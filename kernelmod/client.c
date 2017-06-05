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

#define NETLINK_TRAFFIC_PORT 31

#define MAX_PAYLOAD 1024 /* maximum payload size*/
struct sockaddr_nl src_addr, dest_addr;
struct nlmsghdr *nlh = NULL;
struct iovec iov;
int sock_fd;
struct msghdr msg;

int main()
{
	message_type_t type;
    sock_fd = socket(PF_NETLINK, SOCK_RAW, NETLINK_TRAFFIC_PORT);
    if(sock_fd<0) {
		fprintf(stderr, "Error connecting to socket: %s\n", strerror(errno));
		return -1;
	}

    memset(&src_addr, 0, sizeof(src_addr));
    src_addr.nl_family = AF_NETLINK;
    src_addr.nl_pid = getpid(); /* self pid */

    bind(sock_fd, (struct sockaddr*)&src_addr, sizeof(src_addr));

    memset(&dest_addr, 0, sizeof(dest_addr));
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.nl_family = AF_NETLINK;
    dest_addr.nl_pid = 0; /* For Linux Kernel */
    dest_addr.nl_groups = 0; /* unicast */

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

    /* Read message from kernel */
    while (1) {
		recvmsg(sock_fd, &msg, 0);
		//printf("Received message payload: %s\n", (char *)NLMSG_DATA(nlh));
		pkt_info_t pkt;
		char pkt_str[2048];
		type = wire2pktinfo(&pkt, (unsigned char *)NLMSG_DATA(nlh));
		if (type == SPIN_BLOCKED) {
			pktinfo2str(pkt_str, &pkt, 2048);
			printf("[BLOCKED] %s\n", pkt_str);
		} else if (type == SPIN_TRAFFIC_DATA) {
			pktinfo2str(pkt_str, &pkt, 2048);
			printf("[TRAFFIC] %s\n", pkt_str);
		}
		printf("%s\n", pkt_str);
	}
    close(sock_fd);
}
