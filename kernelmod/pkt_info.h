
#ifndef SPIN_PKT_INFO_H
#define SPIN_PKT_INFO_H 1

#ifdef __KERNEL__
#include <linux/kernel.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netlink.h>

#include <linux/udp.h>
#include <linux/tcp.h>
#include <linux/inet.h>
#include <linux/ip.h>
#include <linux/kernel.h>

#else

#include <sys/socket.h>
#include <sys/types.h>
#include <linux/netlink.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <errno.h>
#include <netinet/in.h>
#endif // __KERNEL


typedef struct packet_info {
	uint8_t family; // 4, 6, etc
	uint8_t protocol; // value for tcp/udp/icmp/etc.
	uint8_t src_addr[16]; // v4 just uses first 4 bytes
	uint8_t dest_addr[16]; // v4 just uses first 4 bytes
	uint16_t src_port;
	uint16_t dest_port;
	uint32_t payload_size;
} pkt_info_t;

size_t pktinfo_wire_size(void);

// Writes a string representation of the given IP address (in
// wire format) to the given destination address.
void ntop(int fam, char* dest, const uint8_t* src, size_t max);

// Writes packet info data to target in string format
// writes until max_len is reached
void pktinfo2str(unsigned char* dest, pkt_info_t* pkt_info, size_t max_len);

// writes packet info data to target in wire format
// target must have pktinfo_wire_size() bytes available
void pktinfo2wire(unsigned char* dest, pkt_info_t* pkt_info);

void wire2pktinfo(pkt_info_t* pkt_info, unsigned char* src);

#endif // SPIN_PKT_INFO
