
#include "pkt_info.h"

size_t pktinfo_msg_size() {
	// msg size (2 octets), message type (1 octet), data
	return pktinfo_wire_size() + 3;
}

size_t dns_pktinfo_msg_size() {
	// msg size (2 octets), message type (1 octet), data
	return dns_pktinfo_wire_size() + 3;
}

size_t pktinfo_wire_size() {
	return sizeof(pkt_info_t);
}

size_t dns_pktinfo_wire_size() {
	return sizeof(dns_pkt_info_t);
}

void ntop(int fam, char* dest, const uint8_t* src, size_t max) {
	if (fam == AF_INET) {
		snprintf(dest, max, "%d.%d.%d.%d", src[0], src[1], src[2], src[3]);
	} else {
		snprintf(dest, max,
				 "%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x",
				 src[0],
				 src[1],
				 src[2],
				 src[3],
				 src[4],
				 src[5],
				 src[6],
				 src[7],
				 src[8],
				 src[9],
				 src[10],
				 src[11],
				 src[12],
				 src[13],
				 src[14],
				 src[15]);
	}
}

void pktinfo2str(unsigned char* dest, pkt_info_t* pkt_info, size_t max_len) {
	char sa[INET6_ADDRSTRLEN];
	char da[INET6_ADDRSTRLEN];
	if (pkt_info->family == AF_INET) {
		ntop(AF_INET, sa, (pkt_info->src_addr) + 12, INET6_ADDRSTRLEN);
		ntop(AF_INET, da, (pkt_info->dest_addr) + 12, INET6_ADDRSTRLEN);
	} else if (pkt_info->family == AF_INET6) {
		ntop(AF_INET6, sa, pkt_info->src_addr, INET6_ADDRSTRLEN);
		ntop(AF_INET6, da, pkt_info->dest_addr, INET6_ADDRSTRLEN);
	} else {
		snprintf(dest, max_len, "<unknown ip version>");
	}

	snprintf(dest, max_len,
	         "ipv%d protocol %d %s:%u -> %s:%u %u bytes",
	         pkt_info->family == AF_INET ? 4 : 6,
	         pkt_info->protocol,
	         sa, ntohs(pkt_info->src_port),
	         da, ntohs(pkt_info->dest_port),
	         ntohl(pkt_info->payload_size));
}

/*
uint32_t read_uint32(uint8_t* src) {
	uint32_t result;
	result = (src[0] << 24) + (src[1] << 16) + (src[2] << 8) + src[3];
	return ntohl(result);
}
*/

void dns_pktinfo2str(unsigned char* dest, dns_pkt_info_t* dns_pkt_info, size_t max_len) {
	uint32_t ttl;
	unsigned char dname[256];
	char ip[INET6_ADDRSTRLEN];
	
	ttl = ntohl(dns_pkt_info->ttl);
	if (dns_pkt_info->family == 4) {
		ntop(AF_INET, ip, dns_pkt_info->ip + 12, INET6_ADDRSTRLEN);
	} else {
		ntop(AF_INET6, ip, dns_pkt_info->ip, INET6_ADDRSTRLEN);
	}
	strncpy(dname, dns_pkt_info->dname, 256);
	
	snprintf(dest, max_len,
			 "[DNS] %s %s %u\n", ip, dname, ttl);
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

void pktinfo_msg2wire(message_type_t type, unsigned char* dest, pkt_info_t* pkt_info) {
	//printf("Write message of type %u size %u\n", SPIN_TRAFFIC_DATA
	// write message type first
	dest[0] = (uint8_t) type;
	dest += 1;
	
	// write the size of the full message
	write_int16(dest, pktinfo_wire_size());
	dest += 2;
	
	pktinfo2wire(dest, pkt_info);
}

message_type_t wire2pktinfo(pkt_info_t* pkt_info, unsigned char* src) {
	// todo: should we read message type and size earlier?
	message_type_t msg_type;
	uint16_t msg_size;
	
	msg_type = src[0];
	if (msg_type == SPIN_TRAFFIC_DATA || msg_type == SPIN_BLOCKED) {
		src++;
		msg_size = read_int16(src);
		src += 2;
		// right now, we have stored everything in network order anyway
		memcpy(pkt_info, src, sizeof(pkt_info_t));
	}
	return msg_type;
}

void dns_pktinfo_msg2wire(unsigned char* dest, dns_pkt_info_t* dns_pkt_info) {
	uint16_t msg_size;
	
	dest[0] = SPIN_DNS_ANSWER;
	dest += 1;
	// spread this out into functions too?
	msg_size = sizeof(dns_pkt_info_t);
	write_int16(dest, htons(msg_size));
	dest += 2;
	// right now, we have stored everything in network order anyway
	memcpy(dest, dns_pkt_info, sizeof(dns_pkt_info_t));
}

message_type_t wire2dns_pktinfo(dns_pkt_info_t* dns_pkt_info, unsigned char* src) {
	// todo: should we read message type and size earlier?
	message_type_t msg_type;
	uint16_t msg_size;
	
	msg_type = src[0];
	if (msg_type == SPIN_DNS_ANSWER) {
		src++;
		msg_size = read_int16(src);
		src += 2;
		// right now, we have stored everything in network order anyway
		memcpy(dns_pkt_info, src, sizeof(dns_pkt_info_t));
	}
	return msg_type;
}
