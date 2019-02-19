#ifdef notdef

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

#include "spin_log.h"

// Do we need a lock around this one?
//int command_sock_fd;

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
    if (command_result != NULL) {
        free(command_result->ips);
        free(command_result);
    }
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
        spin_log(LOG_WARNING, "[XX] unknown ip version\n");
    }
}

int
netlink_command_result_contains_ip(netlink_command_result_t* command_result, ip_t* ip) {
    size_t i;
    if (command_result == NULL) {
        return 0;
    }
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
    struct sockaddr_nl src_addr, dest_addr;
    struct nlmsghdr *nlh = NULL;
    struct iovec iov;
    int sock_fd;
    struct msghdr msg;
    netlink_command_result_t* command_result = netlink_command_result_create();

    sock_fd = socket(PF_NETLINK, SOCK_RAW, NETLINK_CONFIG_PORT);
    if(sock_fd<0) {
        spin_log(LOG_ERR, "Error connecting to socket: %s\n", strerror(errno));
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
            spin_log(LOG_ERR, "protocol mismatch from kernel module: got %u, expected %u\n", version, SPIN_NETLINK_PROTOCOL_VERSION);
            break;
        }

        cmd = ((uint8_t*)NLMSG_DATA(nlh))[1];

        if (cmd == SPIN_CMD_END) {
            break;
        } else if (cmd == SPIN_CMD_ERR) {
            //spin_log(LOG_DEBUG, "Received message payload: %s\n", (char *)NLMSG_DATA(nlh));
            //pkt_info_t pkt;
            char err_str[MAX_NETLINK_PAYLOAD];
            strncpy(err_str, (char *)NLMSG_DATA(nlh) + 2, MAX_NETLINK_PAYLOAD);
            spin_log(LOG_ERR, "Error message from kernel: %s\n", err_str);
        } else if (cmd == SPIN_CMD_IP) {
            // TODO: check message size
            // first octet is ip version (AF_INET or AF_INET6)
            uint8_t ipv = ((uint8_t*)NLMSG_DATA(nlh))[2];
            /*
            unsigned char ip_str[INET6_ADDRSTRLEN];
            inet_ntop(ipv, NLMSG_DATA(nlh) + 3, (char*)ip_str, INET6_ADDRSTRLEN);
            spin_log(LOG_DEBUG, "%s\n", ip_str);
            */
            netlink_command_result_add_ip(command_result, ipv, NLMSG_DATA(nlh) + 3);
        } else {
            spin_log(LOG_WARNING, "unknown command response type received from kernel (%u %02x), stopping\n", cmd, cmd);
            hexdump((uint8_t*)NLMSG_DATA(nlh), nlh->nlmsg_len);
            break;
        }
    }
    close(sock_fd);
    // TODO: can we alloc this once?
    free(nlh);
    return command_result;
}

netlink_command_result_t* send_netlink_command_noarg(config_command_t cmd) {
    unsigned char cmd_buf[2];
    cmd_buf[0] = SPIN_NETLINK_PROTOCOL_VERSION;
    cmd_buf[1] = cmd;
    return send_netlink_command_buf(2, cmd_buf);
}

// should we convert earlier?
netlink_command_result_t* send_netlink_command_iparg(config_command_t cmd, ip_t* ip) {
    unsigned char cmd_buf[19];
    cmd_buf[0] = SPIN_NETLINK_PROTOCOL_VERSION;
    cmd_buf[1] = (uint8_t) cmd;
    cmd_buf[2] = ip->family;
    if (ip == NULL || !(ip->family == AF_INET || ip->family == AF_INET6)) {
        return NULL;
    }
    if (ip->family == AF_INET) {
        memcpy(cmd_buf+3, ip->addr+12, 4);
        return send_netlink_command_buf(7, cmd_buf);
    } else {
        memcpy(cmd_buf+3, ip->addr, 16);
        return send_netlink_command_buf(19, cmd_buf);
    }
}

#endif
