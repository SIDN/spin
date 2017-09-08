
#include "netlink_commands.h"

#include "pkt_info.h"


#include <sys/socket.h>
#include <sys/types.h>
#include <linux/netlink.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>

// Do we need a lock around this one?
int command_sock_fd;

#define NETLINK_CONFIG_PORT 30

#define MAX_NETLINK_PAYLOAD 1024 /* maximum payload size*/


netlink_command_result_t* netlink_command_result_create(void) {
    netlink_command_result_t* command_result = (netlink_command_result_t*)malloc(sizeof(netlink_command_result_t));
    command_result->ip_max = 10;
    command_result->ips = malloc(sizeof(ip_t) * command_result->ip_max);
    command_result->ip_count = 0;
    command_result->error = NULL;

    return command_result;
}

void netlink_command_result_destroy(netlink_command_result_t* command_result) {
    free(command_result->ips);
    free(command_result);
}

void netlink_command_result_add_ip(netlink_command_result_t* command_result, uint8_t ip_fam, uint8_t* ip) {
    //uint8_t* s_ip;
    if (command_result->ip_count + 1 >= command_result->ip_max) {
        command_result->ip_max = command_result->ip_max * 2;
        command_result->ips = realloc(command_result->ips, sizeof(ip_t) * command_result->ip_max);
    }
    // we store it in the sizeof(ip_t)-bit 'format' of spin here
    if (ip_fam == AF_INET) {
        command_result->ips[command_result->ip_count].family = ip_fam;
        memset(command_result->ips[command_result->ip_count].addr, 0, 12);
        memcpy(&command_result->ips[command_result->ip_count].addr[12], ip, 4);
        command_result->ip_count++;
    } else if (ip_fam == AF_INET6) {
        command_result->ips[command_result->ip_count].family = ip_fam;
        memcpy(command_result->ips[command_result->ip_count].addr, ip, 16);
        command_result->ip_count++;
    } else {
        printf("[XX] error: unknown ip version\n");
    }
}

int
netlink_command_result_contains_ip(netlink_command_result_t* command_result, ip_t* ip) {
    size_t i;
    for (i = 0; i < command_result->ip_count; i++) {
        if (memcmp(ip, &command_result->ips[i], sizeof(ip_t)) == 0) {
            return 1;
        }
    }
    return 0;
}


void netlink_command_result_set_error(netlink_command_result_t* command_result, char* error) {
    command_result->error = error;
}


netlink_command_result_t*
send_netlink_command_buf(size_t cmdbuf_size, unsigned char* cmdbuf)
{
    config_command_t cmd;
    uint8_t version;
    netlink_command_result_t* command_result = netlink_command_result_create();
    struct sockaddr_nl src_addr, dest_addr;
    struct nlmsghdr *command_nlh = NULL;
    struct iovec command_iov;
    struct msghdr command_msg;

    command_sock_fd = socket(PF_NETLINK, SOCK_RAW, NETLINK_CONFIG_PORT);
    if(command_sock_fd < 0) {
        fprintf(stderr, "Error connecting to socket: %s\n", strerror(errno));
        netlink_command_result_set_error(command_result, "Error connecting to netlink socket");
        return command_result;
    }

    memset(&src_addr, 0, sizeof(src_addr));
    src_addr.nl_family = AF_NETLINK;
    src_addr.nl_pid = getpid(); /* self pid */

    bind(command_sock_fd, (struct sockaddr*)&src_addr, sizeof(src_addr));

    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.nl_family = AF_NETLINK;
    dest_addr.nl_pid = 0; /* For Linux Kernel */
    dest_addr.nl_groups = 0; /* unicast */

    // TODO: can we alloc this once?
    command_nlh = (struct nlmsghdr *)malloc(NLMSG_SPACE(MAX_NETLINK_PAYLOAD));
    memset(command_nlh, 0, NLMSG_SPACE(MAX_NETLINK_PAYLOAD));
    command_nlh->nlmsg_len = NLMSG_SPACE(MAX_NETLINK_PAYLOAD);
    command_nlh->nlmsg_pid = getpid();
    command_nlh->nlmsg_flags = 0;

    memcpy(NLMSG_DATA(command_nlh), cmdbuf, cmdbuf_size);

    command_iov.iov_base = (void *)command_nlh;
    command_iov.iov_len = command_nlh->nlmsg_len;
    command_msg.msg_name = (void *)&dest_addr;
    command_msg.msg_namelen = sizeof(dest_addr);
    command_msg.msg_iov = &command_iov;
    command_msg.msg_iovlen = 1;

    // set it shorter when sending
    command_nlh->nlmsg_len = NLMSG_SPACE(cmdbuf_size);
    sendmsg(command_sock_fd,&command_msg,0);
    // max len again, we don't know response sizes
    command_nlh->nlmsg_len = NLMSG_SPACE(MAX_NETLINK_PAYLOAD);

    /* Read response message(s) */
    // Last message should always be SPIN_CMD_END with no data
    while (1) {
        recvmsg(command_sock_fd, &command_msg, 0);
        version = ((uint8_t*)NLMSG_DATA(command_nlh))[0];
        if (version != 1) {
            printf("protocol mismatch from kernel module: got %u, expected %u\n", version, SPIN_NETLINK_PROTOCOL_VERSION);
            break;
        }

        cmd = ((uint8_t*)NLMSG_DATA(command_nlh))[1];

        if (cmd == SPIN_CMD_END) {
            printf("[XX] SPIN_CMD_END\n");
            break;
        } else if (cmd == SPIN_CMD_ERR) {
            printf("[XX] SPIN_CMD_ERR\n");
            //printf("Received message payload: %s\n", (char *)NLMSG_DATA(nlh));
            //pkt_info_t pkt;
            char err_str[MAX_NETLINK_PAYLOAD];
            strncpy(err_str, (char *)NLMSG_DATA(command_nlh) + 2, MAX_NETLINK_PAYLOAD);
            printf("Error message from kernel: %s\n", err_str);
        } else if (cmd == SPIN_CMD_IP) {
            printf("[XX] SPIN_CMD_IP\n");
            // TODO: check message size
            // first octet is ip version (AF_INET or AF_INET6)
            uint8_t ipv = ((uint8_t*)NLMSG_DATA(command_nlh))[2];
            unsigned char ip_str[INET6_ADDRSTRLEN];
            inet_ntop(ipv, NLMSG_DATA(command_nlh) + 3, (char*)ip_str, INET6_ADDRSTRLEN);
            printf("%s\n", ip_str);
            netlink_command_result_add_ip(command_result, ipv, NLMSG_DATA(command_nlh) + 3);
        } else {
            printf("unknown command response type received from kernel (%u %02x), stopping\n", cmd, cmd);
            hexdump((uint8_t*)NLMSG_DATA(command_nlh), command_nlh->nlmsg_len);
            break;
        }
    }
    close(command_sock_fd);
    // TODO: can we alloc this once?
    free(command_nlh);
    return command_result;
}

netlink_command_result_t* send_netlink_command_noarg(config_command_t cmd) {
    unsigned char cmd_buf[2];
    cmd_buf[0] = SPIN_NETLINK_PROTOCOL_VERSION;
    cmd_buf[1] = cmd;
    return send_netlink_command_buf(2, cmd_buf);
}

// should we convert earlier?
netlink_command_result_t* send_netlink_command_iparg(config_command_t cmd, uint8_t* ip) {
    unsigned char cmd_buf[19];
    cmd_buf[0] = SPIN_NETLINK_PROTOCOL_VERSION;
    cmd_buf[1] = (uint8_t) cmd;
    cmd_buf[2] = ip[0];
    if (ip[0] == AF_INET) {
        memcpy(cmd_buf+3, ip+13, 4);
        return send_netlink_command_buf(7, cmd_buf);
    } else {
        memcpy(cmd_buf+3, ip+3, 16);
        return send_netlink_command_buf(19, cmd_buf);
    }
}

