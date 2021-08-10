/**
 * Shared functions and structures for information about packets
 */
#ifndef SPIN_PKT_INFO_H
#define SPIN_PKT_INFO_H 1

#ifdef __KERNEL__
#include <linux/kernel.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>

#include <linux/ctype.h>
#include <linux/udp.h>
#include <linux/tcp.h>
#include <linux/inet.h>
#include <linux/ip.h>

#else

#include <sys/socket.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <errno.h>
#include <netinet/in.h>
#include <ctype.h>
#endif // __KERNEL

typedef enum {
	SPIN_TRAFFIC_DATA = 1,
	SPIN_DNS_ANSWER = 2,
	SPIN_BLOCKED = 3,
	SPIN_DNS_QUERY = 4,
	SPIN_ERR_BADVERSION = 250
} message_type_t;


// Note: when changing this structure, check pkt_info_equals,
// which assumes all equality data is in the first 39 bytes
typedef struct packet_info {
	uint8_t family; // 4, 6, etc
	uint8_t protocol; // value for tcp/udp/icmp/etc.
	uint8_t src_addr[16]; // v4 just uses last 4 bytes
	uint8_t dest_addr[16]; // v4 just uses last 4 bytes
	// Source port (in host endianness)
	uint16_t src_port;
	// Destination port (in host endianness)
	uint16_t dest_port;
	uint8_t icmp_type;
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

// Writes a string representation of the given IP address (in
// wire format) to the given destination address.
void ntop(int fam, char* dest, const uint8_t* src, size_t max);

// Writes packet info data to target in string format
// writes until max_len is reached
void pktinfo2str(char* dest, pkt_info_t* pkt_info, size_t max_len);

// returns true if the packets are considered equal
// (same family, protocol, addresses and ports)
int pkt_info_equal(pkt_info_t* a, pkt_info_t* b);

void dns_pktinfo2str(char* dest, dns_pkt_info_t* dns_pkt_info, size_t max_len);
void dns_dname2str(char* dest, char* src, size_t max_len);


#endif // SPIN_PKT_INFO
