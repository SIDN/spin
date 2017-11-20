//Taken from https://stackoverflow.com/questions/15215865/netlink-sockets-in-c-using-the-3-x-linux-kernel?lq=1

#include <sys/socket.h>
#include <sys/types.h>
#include <linux/netlink.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include <errno.h>

#include "pkt_info.h"
#include "spin_log.h"

#include <poll.h>

#define NETLINK_TRAFFIC_PORT 31

#define MAX_PAYLOAD 1024 /* maximum payload size*/
struct sockaddr_nl src_addr, dest_addr;
struct nlmsghdr *nlh = NULL;
struct iovec iov;
int sock_fd;
struct msghdr msg;

int ack_counter;

void hexdump(uint8_t* data, unsigned int size) {
    unsigned int i;
    printf("00: ");
    for (i = 0; i < size; i++) {
        if (i > 0 && i % 10 == 0) {
            printf("\n%u: ", i);
        }
        printf("%02x ", data[i]);
    }
    printf("\n");
}

void send_ack()
{
    printf("[XX] send_ack called\n");
    nlh = (struct nlmsghdr *)malloc(NLMSG_SPACE(MAX_PAYLOAD));
    memset(nlh, 0, NLMSG_SPACE(MAX_PAYLOAD));
    nlh->nlmsg_len = NLMSG_SPACE(MAX_PAYLOAD);
    nlh->nlmsg_pid = getpid();
    nlh->nlmsg_flags = 0;

    strcpy(NLMSG_DATA(nlh), "Hello!");

    iov.iov_base = (void *)nlh;
    iov.iov_len = nlh->nlmsg_len;
    msg.msg_name = (void *)&dest_addr;
    msg.msg_namelen = sizeof(dest_addr);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    printf("Sending ACK to kernel\n");
    sendmsg(sock_fd,&msg,0);
    printf("[XX] send_ack done\n");
}

#define MESSAGES_BEFORE_WAIT 50
void check_send_ack() {
    printf("[XX] check_send_ack called\n");
    ack_counter++;
    if (ack_counter > MESSAGES_BEFORE_WAIT) {
        send_ack();
        ack_counter = 0;
    }
    printf("[XX] check_send_ack done\n");
}

int main()
{
    ssize_t c = 0;
    int rs;
    message_type_t type;
    struct timeval tv;
    struct pollfd fds[1];

    spin_log_init(0, 6, NULL);

    ack_counter = 0;

    sock_fd = socket(PF_NETLINK, SOCK_RAW, NETLINK_TRAFFIC_PORT);
    if(sock_fd<0) {
        fprintf(stderr, "Error connecting to socket: %s\n", strerror(errno));
        return -1;
    }

    tv.tv_sec = 0;
    tv.tv_usec = 500;
    setsockopt(sock_fd, 270, SO_RCVTIMEO, (const char*)&tv,sizeof(struct timeval));

    memset(&src_addr, 0, sizeof(src_addr));
    src_addr.nl_family = AF_NETLINK;
    src_addr.nl_pid = getpid(); /* self pid */

    bind(sock_fd, (struct sockaddr*)&src_addr, sizeof(src_addr));

    memset(&dest_addr, 0, sizeof(dest_addr));
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.nl_family = AF_NETLINK;
    dest_addr.nl_pid = 0; /* For Linux Kernel */
    dest_addr.nl_groups = 0; /* unicast */
/*
    nlh = (struct nlmsghdr *)malloc(NLMSG_SPACE(MAX_PAYLOAD));
    memset(nlh, 0, NLMSG_SPACE(MAX_PAYLOAD));
    nlh->nlmsg_len = NLMSG_SPACE(MAX_PAYLOAD);
    nlh->nlmsg_pid = getpid();
    nlh->nlmsg_flags = 0;

    strcpy(NLMSG_DATA(nlh), "Hello!");

    iov.iov_base = (void *)nlh;
    iov.iov_len = nlh->nlmsg_len;
    msg.msg_name = (void *)&dest_addr;
    msg.msg_namelen = sizeof(dest_addr);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    printf("Sending message to kernel\n");
    sendmsg(sock_fd,&msg,0);
    printf("Waiting for message from kernel\n");
*/
    send_ack();

    fds[0].fd = sock_fd;
    fds[0].events = POLLIN;

    /* Read message from kernel */
    while (1) {
        rs = poll(fds, 1, 500);
        if (rs == 0) {
            continue;
        }

        rs = recvmsg(sock_fd, &msg, 0);
        printf("[XX] got msg\n");
        if (rs < 0) {
            continue;
        }
        c++;
        //printf("C: %u RS: %u\n", c, rs);
        //printf("Received message payload: %s\n", (char *)NLMSG_DATA(nlh));
        pkt_info_t pkt;
        dns_pkt_info_t dns_pkt;
        char pkt_str[1024];
        printf("[XX] call wire2pktinfo\n");
        type = wire2pktinfo(&pkt, (unsigned char *)NLMSG_DATA(nlh));
        if (type == SPIN_BLOCKED) {
            printf("[XX] call pktinfo2str (blocked)\n");
            pktinfo2str(pkt_str, &pkt, 1024);
            printf("[XX] pktinfo2str done (size %u)\n", strlen(pkt_str));
            printf("[BLOCKED] %s\n", pkt_str);
            check_send_ack();
        } else if (type == SPIN_TRAFFIC_DATA) {
            printf("[XX] call pktinfo2str (traffic)\n");
            pktinfo2str(pkt_str, &pkt, 1024);
            printf("[XX] pktinfo2str done (size %u)\n", strlen(pkt_str));
            printf("[TRAFFIC] %s\n", pkt_str);
            check_send_ack();
        } else if (type == SPIN_DNS_ANSWER) {
            // note: bad version would have been caught in wire2pktinfo
            // in this specific case
            printf("[XX] call wire2dns_pktinfo\n");
            wire2dns_pktinfo(&dns_pkt, (unsigned char *)NLMSG_DATA(nlh));
            printf("[XX] wire2dns_pktinfo done, call  dns_pktinfo2str\n");
            dns_pktinfo2str(pkt_str, &dns_pkt, 1024);
            printf("[XX] dns_pktinfo2str done (size %u)\n", strlen(pkt_str));
            printf("[DNS] %s\n", pkt_str);
            check_send_ack();
        } else if (type == SPIN_ERR_BADVERSION) {
            printf("Error: version mismatch between client and kernel module\n");
        } else {
            printf("unknown type? %u\n", type);
        }
    }
    close(sock_fd);
}
