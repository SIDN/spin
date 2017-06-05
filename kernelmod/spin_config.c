#include <sys/socket.h>
#include <sys/types.h>
#include <linux/netlink.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include <errno.h>

#include "pkt_info.h"
#include "spin_cfg.h"

#define NETLINK_CONFIG_PORT 30

#define MAX_PAYLOAD 1024 /* maximum payload size*/
struct sockaddr_nl src_addr, dest_addr;
struct nlmsghdr *nlh = NULL;
struct iovec iov;
int sock_fd;
struct msghdr msg;

int main()
{
	config_command_t cmd;
	
    sock_fd = socket(PF_NETLINK, SOCK_RAW, NETLINK_CONFIG_PORT);
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
		cmd = ((uint8_t*)NLMSG_DATA(nlh))[0];
		
		if (cmd == SPIN_CMD_END) {
			printf("[XX] received end command, quit\n");
			break;
		} else if (cmd == SPIN_CMD_ERR) {
			//printf("Received message payload: %s\n", (char *)NLMSG_DATA(nlh));
			pkt_info_t pkt;
			char err_str[MAX_PAYLOAD];
			strncpy(err_str, (char *)NLMSG_DATA(nlh) + 1, MAX_PAYLOAD);
			printf("Error message from kernel: %s\n", err_str);
		} else if (cmd == SPIN_CMD_GET_IGNORE) {
			// TODO: check message size
			// first octet is ip version (AF_INET or AF_INET6)
			uint8_t ipv = ((uint8_t*)NLMSG_DATA(nlh))[1];
			unsigned char ip_str[INET6_ADDRSTRLEN];
			inet_ntop(ipv, NLMSG_DATA(nlh) + 2, ip_str, INET6_ADDRSTRLEN);
			printf("Ignore: %s\n", ip_str);
		} else {
			printf("unknown command response type received from kernel, stopping\n");
			break;
		}
	}
    close(sock_fd);
}

