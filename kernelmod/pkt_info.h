/**
 * Shared functions and structures for communication between
 * kernel module and client(s)
 */
#ifndef SPIN_PKT_INFO_H
#define SPIN_PKT_INFO_H 1

#ifdef __KERNEL__
#include <linux/kernel.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netlink.h>

#include <linux/ctype.h>
#include <linux/udp.h>
#include <linux/tcp.h>
#include <linux/inet.h>
#include <linux/ip.h>

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
#include <ctype.h>
#endif // __KERNEL

#define SPIN_NETLINK_PROTOCOL_VERSION 1

typedef enum {
	SPIN_TRAFFIC_DATA = 1,
	SPIN_DNS_ANSWER = 2,
	SPIN_BLOCKED = 3,
	SPIN_DNS_QUERY = 4,
	SPIN_ERR_BADVERSION = 250
} message_type_t;


// Note: when changing this structure, check pkt_info_equals,
// which assumes all equality data is in the first 38 bytes
typedef struct packet_info {
	uint8_t family; // 4, 6, etc
	uint8_t protocol; // value for tcp/udp/icmp/etc.
	uint8_t src_addr[16]; // v4 just uses last 4 bytes
	uint8_t dest_addr[16]; // v4 just uses last 4 bytes
	uint16_t src_port;
	uint16_t dest_port;
	uint64_t payload_size;
	uint64_t packet_count; // amount of packets for this set of
	                       // fam, proto, source, dest, and ports
	uint16_t payload_offset; // only relevant if packet_count == 1
} pkt_info_t;

// note: all values are stored in network order
// family is 4 or 6
// dns_cache and node_cache assume first 17 bytes are family + address
typedef struct dns_packet_info {
	uint8_t family;
	uint8_t ip[16];
	uint32_t ttl;
	char dname[256];
} dns_pkt_info_t;

// Size of the full pktinfo wire message (header + content)
size_t pktinfo_msg_size(void);

size_t dns_pktinfo_msg_size(void);

// Size of the pktinfo wire data itself (without header)
size_t pktinfo_wire_size(void);

size_t dns_pktinfo_wire_size(void);

// Writes a string representation of the given IP address (in
// wire format) to the given destination address.
void ntop(int fam, char* dest, const uint8_t* src, size_t max);

// Writes packet info data to target in string format
// writes until max_len is reached
void pktinfo2str(char* dest, pkt_info_t* pkt_info, size_t max_len);

// writes packet info data to target in wire format
// target must have pktinfo_wire_size() bytes available
void pktinfo2wire(uint8_t* dest, pkt_info_t* pkt_info);

// returns true if the packets are considered equal
// (same family, protocol, addresses and ports)
int pkt_info_equal(pkt_info_t* a, pkt_info_t* b);

// writes full packet info message (header + pktinfo)
// target must have pktinfo_msg_size() bytes available
void pktinfo_msg2wire(message_type_t type, uint8_t* dest, pkt_info_t* pkt_info);

// Reads pkt_info data from memory at src
// Returns the message type. If the type is BLOCKED or TRAFFIC,
// pkt_info will be filled with info about the blocked or traffic data
message_type_t wire2pktinfo(pkt_info_t* pkt_info, char* src);

void dns_pktinfo_msg2wire(message_type_t type, uint8_t* dest, dns_pkt_info_t* pkt_info);
void dns_pktinfo2str(char* dest, dns_pkt_info_t* dns_pkt_info, size_t max_len);
void dns_pktinfo2wire(uint8_t* dest, dns_pkt_info_t* dns_pkt_info);
void dns_dname2str(char* dest, char* src, size_t max_len);
message_type_t wire2dns_pktinfo(dns_pkt_info_t* dns_pkt_info, char* src);

#endif // SPIN_PKT_INFO
