#include <sys/socket.h>
#include <sys/types.h>
#include <linux/netlink.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>

#include <errno.h>
#include <string.h>

#include <pkt_info.h>
#include <spin_cfg.h>
#include <tree.h>
#include <util.h>

#define NETLINK_CONFIG_PORT 30

#define MAX_NETLINK_PAYLOAD 1024 /* maximum payload size*/
#define LINE_MAX 1024


typedef enum {
    IGNORE,
    BLOCK,
    EXCEPT
} cmd_types_t;

typedef enum {
    SHOW,
    ADD,
    REMOVE,
    CLEAR,
    LOAD,
    SAVE
} cmd_t;

int
send_command(size_t cmdbuf_size, unsigned char* cmdbuf, FILE* output)
{
    struct sockaddr_nl src_addr, dest_addr;
    struct nlmsghdr *nlh = NULL;
    struct iovec iov;
    struct msghdr msg;
    int sock_fd;
    config_command_t cmd;
    uint8_t version;

    sock_fd = socket(PF_NETLINK, SOCK_RAW, NETLINK_CONFIG_PORT);
    if(sock_fd<0) {
        fprintf(stderr, "Error connecting to socket: %s\n", strerror(errno));
        return 0;
    }

    memset(&src_addr, 0, sizeof(src_addr));
    src_addr.nl_family = AF_NETLINK;
    src_addr.nl_pid = getpid(); /* self pid */

    bind(sock_fd, (struct sockaddr*)&src_addr, sizeof(src_addr));

    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.nl_family = AF_NETLINK;
    dest_addr.nl_pid = 0; /* For Linux Kernel */
    dest_addr.nl_groups = 0; /* unicast */

    nlh = (struct nlmsghdr *)malloc(NLMSG_SPACE(MAX_NETLINK_PAYLOAD));
    memset(nlh, 0, NLMSG_SPACE(MAX_NETLINK_PAYLOAD));
    nlh->nlmsg_len = NLMSG_SPACE(MAX_NETLINK_PAYLOAD);
    nlh->nlmsg_pid = getpid();
    nlh->nlmsg_flags = 0;

    memcpy(NLMSG_DATA(nlh), cmdbuf, cmdbuf_size);

    iov.iov_base = (void *)nlh;
    iov.iov_len = nlh->nlmsg_len;
    memset(&msg, 0, sizeof(struct msghdr));
    msg.msg_name = (void *)&dest_addr;
    msg.msg_namelen = sizeof(dest_addr);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    // set it shorter when sending
    nlh->nlmsg_len = NLMSG_SPACE(cmdbuf_size);
    sendmsg(sock_fd,&msg,0);
    // max len again, we don't know response sizes
    nlh->nlmsg_len = NLMSG_SPACE(MAX_NETLINK_PAYLOAD);

    /* Read response message(s) */
    // Last message should always be SPIN_CMD_END with no data
    while (1) {
        recvmsg(sock_fd, &msg, 0);
        version = ((uint8_t*)NLMSG_DATA(nlh))[0];
        if (version != 1) {
            printf("protocol mismatch from kernel module: got %u, expected %u\n", version, SPIN_NETLINK_PROTOCOL_VERSION);
            break;
        }

        cmd = ((uint8_t*)NLMSG_DATA(nlh))[1];

        if (cmd == SPIN_CMD_END) {
            break;
        } else if (cmd == SPIN_CMD_ERR) {
            //printf("Received message payload: %s\n", (char *)NLMSG_DATA(nlh));
            char err_str[MAX_NETLINK_PAYLOAD];
            strncpy(err_str, (char *)NLMSG_DATA(nlh) + 2, MAX_NETLINK_PAYLOAD);
            printf("Error message from kernel: %s\n", err_str);
        } else if (cmd == SPIN_CMD_IP) {
            // TODO: check message size
            // first octet is ip version (AF_INET or AF_INET6)
            uint8_t ipv = ((uint8_t*)NLMSG_DATA(nlh))[2];
            char ip_str[INET6_ADDRSTRLEN];
            inet_ntop(ipv, NLMSG_DATA(nlh) + 3, ip_str, INET6_ADDRSTRLEN);
            if (output != NULL) {
                fprintf(output, "%s\n", ip_str);
            }
        } else {
            printf("unknown command response type received from kernel (%u %02x), stopping\n", cmd, cmd);
            hexdump((uint8_t*)NLMSG_DATA(nlh), nlh->nlmsg_len);
            break;
        }
    }
    close(sock_fd);
    return 1;
}

void help(int rcode) {
    printf("Usage: spin_config <type> <command> [address/file] [options]\n");
    printf("Types:\n");
    printf("- ignore: show or modify the list of addresses that are ignored\n");
    printf("- block:  show or modify the list of addresses that are blocked\n");
    printf("- except: show or modify the list of addresses that are not blocked\n");
    printf("Commands:\n");
    printf("- show:   show the addresses in the list\n");
    printf("- add:    add address to list\n");
    printf("- remove: remove address from list\n");
    printf("- clear:  remove all addresses from list\n");
    printf("- load:   clear the list and add all addresses from the given file\n");
    printf("- save:   store all current addresses from the given file\n");
    printf("If no options are given, show all lists\n");
    exit(rcode);
}

int
execute_no_arg(cmd_types_t type, cmd_t cmd, FILE* output) {
    unsigned char cmdbuf[2];
    cmdbuf[0] = SPIN_NETLINK_PROTOCOL_VERSION;

    switch (type) {
    case IGNORE:
        switch (cmd) {
        case SHOW:
            cmdbuf[1] = SPIN_CMD_GET_IGNORE;
            break;
        case CLEAR:
            cmdbuf[1] = SPIN_CMD_CLEAR_IGNORE;
            break;
        default:
            // Called with wrong argument
            return -1;
        }
        break;
    case BLOCK:
        switch (cmd) {
        case SHOW:
            cmdbuf[1] = SPIN_CMD_GET_BLOCK;
            break;
        case CLEAR:
            cmdbuf[1] = SPIN_CMD_CLEAR_BLOCK;
            break;
        default:
            // Called with wrong argument
            return -1;
        }
        break;
    case EXCEPT:
        switch (cmd) {
        case SHOW:
            cmdbuf[1] = SPIN_CMD_GET_EXCEPT;
            break;
        case CLEAR:
            cmdbuf[1] = SPIN_CMD_CLEAR_EXCEPT;
            break;
        default:
            // Called with wrong argument
            return -1;
        }
        break;
    }
    return send_command(2, cmdbuf, output);
}

int
execute_arg(cmd_types_t type, cmd_t cmd, const char* ip_str, FILE* output) {
    unsigned char cmdbuf[19];
    size_t cmdsize = 19;
    cmdbuf[0] = SPIN_NETLINK_PROTOCOL_VERSION;

    switch (type) {
    case IGNORE:
        switch (cmd) {
        case ADD:
            cmdbuf[1] = SPIN_CMD_ADD_IGNORE;
            break;
        case REMOVE:
            cmdbuf[1] = SPIN_CMD_REMOVE_IGNORE;
            break;
        default:
            // Called with wrong argument
            return -1;
        }
        break;
    case BLOCK:
        switch (cmd) {
        case ADD:
            cmdbuf[1] = SPIN_CMD_ADD_BLOCK;
            break;
        case REMOVE:
            cmdbuf[1] = SPIN_CMD_REMOVE_BLOCK;
            break;
        default:
            // Called with wrong argument
            return -1;
        }
        break;
    case EXCEPT:
        switch (cmd) {
        case ADD:
            cmdbuf[1] = SPIN_CMD_ADD_EXCEPT;
            break;
        case REMOVE:
            cmdbuf[1] = SPIN_CMD_REMOVE_EXCEPT;
            break;
        default:
            // Called with wrong argument
            return -1;
        }
    }

    if (inet_pton(AF_INET6, ip_str, cmdbuf+3) == 1) {
        cmdbuf[2] = AF_INET6;
    } else if (inet_pton(AF_INET, ip_str, cmdbuf+3) == 1) {
        cmdbuf[2] = AF_INET;
        cmdsize = 7;
    } else {
        return 0;
    }

    return send_command(cmdsize, cmdbuf, output);
}

void show_all_lists() {};

int main(int argc, char** argv) {
    // first option is type: ignore, block, exceptions, no option is list all
    // section option is command: list, add, remove, clear
    // third option depends on previous (optional ip address)
    cmd_types_t type;
    cmd_t cmd;
    int i;
    int result;
    FILE* file;
    char *line, *rline;
    unsigned char buf[sizeof(struct in6_addr)];

    if (argc == 1) {
        show_all_lists();
        exit(0);
    }
    if (argc < 3) {
        help(1);
    }

    if (strncmp(argv[1], "ignore", 7) == 0) {
        type = IGNORE;
    } else if (strncmp(argv[1], "block", 6) == 0) {
        type = BLOCK;
    } else if (strncmp(argv[1], "except", 7) == 0) {
        type = EXCEPT;
    } else if (strncmp(argv[1], "-h", 3) == 0) {
        help(0);
    } else if (strncmp(argv[1], "-help", 6) == 0) {
        help(0);
    } else {
        printf("unknown command type %s; must be one of 'ignore', 'block' or 'except'\n", argv[1]);
        return 1;
    }

    if (strncmp(argv[2], "show", 5) == 0) {
        cmd = SHOW;
    } else if (strncmp(argv[2], "add", 4) == 0) {
        cmd = ADD;
    } else if (strncmp(argv[2], "remove", 7) == 0) {
        cmd = REMOVE;
    } else if (strncmp(argv[2], "clear", 6) == 0) {
        cmd = CLEAR;
    } else if (strncmp(argv[2], "load", 5) == 0) {
        cmd = LOAD;
    } else if (strncmp(argv[2], "save", 5) == 0) {
        cmd = SAVE;
    } else if (strncmp(argv[2], "-h", 3) == 0) {
        help(0);
    } else if (strncmp(argv[2], "-help", 6) == 0) {
        help(0);
    } else {
        printf("unknown command type %s; must be one of 'ignore', 'block' or 'except'\n", argv[2]);
        return 1;
    }

    if (cmd == SHOW || cmd == CLEAR) {
        if (argc > 3) {
            printf("Extraneous argument; show and clear take no address\n");
            exit(1);
        }
        execute_no_arg(type, cmd, stdout);
    }
    if (cmd == ADD || cmd == REMOVE) {
        if (argc < 4) {
            printf("Missing argument(s); add and remove take IP addresses\n");
            exit(1);
        }
        // check all addresses
        for (i = 3; i < argc; i++) {
            if (inet_pton(AF_INET6, argv[i], buf) < 1 &&
                inet_pton(AF_INET, argv[i], buf) < 1) {
                printf("Bad IP address: %s, not executing command(s)\n", argv[i]);
                exit(1);
            }
        }
        for (i = 3; i < argc; i++) {
            execute_arg(type, cmd, argv[i], stdout);
        }
    }
    if (cmd == LOAD) {
        if (argc < 4) {
            printf("Missing argument(s); load and save need a file\n");
            exit(1);
        }
        file = fopen(argv[3], "r");
        if (file == NULL) {
            if (errno == ENOENT) {
                printf("No addresses set in %s\n", argv[3]);
                return 0;
            } else {
                printf("Error reading %s: %s (%d)\n", argv[3], strerror(errno), errno);
                // it's ok if it does not exist
                return errno;
            }
        }
        if (!execute_no_arg(type, CLEAR, stdout)) {
            printf("Is the kernel module loaded?\n");
            exit(2);
        }
        line = malloc(LINE_MAX);
        rline = fgets(line, LINE_MAX, file);
        result = 0;
        while (rline != NULL) {
            if (index(rline, '\n') >= 0) {
                *index(rline, '\n') = '\0';
                if (execute_arg(type, ADD, line, stdout)) {
                    result++;
                }
            }
            rline = fgets(line, LINE_MAX, file);
        }
        free(line);
        printf("Added %d addresses from %s\n", result, argv[3]);
    }
    if (cmd == SAVE) {
        if (argc < 4) {
            printf("Missing argument(s); load and save need a file\n");
            exit(1);
        }
        file = fopen(argv[3], "w");
        if (!execute_no_arg(type, SHOW, file)) {
            printf("Is the kernel module loaded?\n");
            exit(2);
        }
        fclose(file);
    }

    return 0;
}
