
#include "pkt_info.h"

size_t pktinfo_msg_size() {
	// msg size (2 octets), message type (1 octet), data
	return pktinfo_wire_size() + 3;
}

size_t pktinfo_wire_size() {
	return sizeof(pkt_info_t);
}

void ntop(int fam, char* dest, const uint8_t* src, size_t max) {
	snprintf(dest, max, "%d.%d.%d.%d", src[0], src[1], src[2], src[3]);
}

void pktinfo2str(unsigned char* dest, pkt_info_t* pkt_info, size_t max_len) {
	char sa[INET6_ADDRSTRLEN];
	char da[INET6_ADDRSTRLEN];
	ntop(AF_INET, sa, pkt_info->src_addr, INET6_ADDRSTRLEN);
	ntop(AF_INET, da, pkt_info->dest_addr, INET6_ADDRSTRLEN);
	snprintf(dest, max_len,
	         "got packet ipv%d protocol %d from %s:%u to %s:%u size %u",
	         pkt_info->family,
	         pkt_info->protocol,
	         sa, ntohs(pkt_info->src_port),
	         da, ntohs(pkt_info->dest_port),
	         ntohl(pkt_info->payload_size));
}

static inline void write_int16(unsigned char* dest, uint16_t i) {
	const uint16_t wi = htons(i);
	memcpy(dest, &wi, 2);
}

static inline uint16_t read_int16(unsigned char* src) {
	uint16_t wi;
	memcpy(&wi, src, 2);
	return ntohs(wi);
}

void pktinfo2wire(unsigned char* dest, pkt_info_t* pkt_info) {
	// right now, we have stored everything in network order anyway
	memcpy(dest, pkt_info, sizeof(pkt_info_t));
}

void pktinfo_msg2wire(unsigned char* dest, pkt_info_t* pkt_info) {
	//printf("Write message of type %u size %u\n", SPIN_TRAFFIC_DATA
	// write message type first
	dest[0] = (uint8_t) SPIN_TRAFFIC_DATA;
	dest += 1;
	
	// write the size of the full message
	write_int16(dest, pktinfo_wire_size());
	dest += 2;
	
	pktinfo2wire(dest, pkt_info);
}

void wire2pktinfo(pkt_info_t* pkt_info, unsigned char* src) {
	// todo: should we read message type and size earlier?
	message_type msg_type;
	uint16_t msg_size;
	
	msg_type = src[0];
	src++;
	msg_size = read_int16(src);
	src += 2;
	// right now, we have stored everything in network order anyway
	memcpy(pkt_info, src, sizeof(pkt_info_t));
}

