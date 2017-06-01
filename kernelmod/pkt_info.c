
#include "pkt_info.h"

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


void pktinfo2wire(unsigned char* dest, pkt_info_t* pkt_info) {
	// right now, we have stored everything in network order anyway
	memcpy(dest, pkt_info, sizeof(pkt_info_t));
}

void wire2pktinfo(pkt_info_t* pkt_info, unsigned char* src) {
	// right now, we have stored everything in network order anyway
	memcpy(pkt_info, src, sizeof(pkt_info_t));
}
