
#include "pkt_info.h"


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

void pktinfo2str(char* dest, pkt_info_t* pkt_info, size_t max_len) {
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
             "ipv%d protocol %d %s:%u -> %s:%u %llu packets %llu bytes",
             pkt_info->family == AF_INET ? 4 : 6,
             pkt_info->protocol,
             sa, pkt_info->src_port,
             da, pkt_info->dest_port,
             (long long unsigned int)pkt_info->packet_count,
             (long long unsigned int)pkt_info->payload_size);
}

void dns_dname2str(char* dname, char* src, size_t max_len) {
    size_t strpos = 0, pos = 0;
    size_t lpos;
    uint8_t labellen, c;

    labellen = src[pos++];
    if (labellen == 0) {
        dname[strpos++] = '.';
    } else {
        while (labellen > 0) {
            for (lpos = 0; lpos < labellen; lpos++) {
                c = src[lpos + pos];
                if(c == '.' || c == ';' ||
                   c == '(' || c == ')' ||
                   c == '\\') {
                    dname[strpos++] = '\\';
                    dname[strpos++] = c;
                } else if (!(isascii(c) && isgraph(c))) {
                    sprintf(&dname[strpos], "\\%03u", c);
                    strpos += 4;
                } else {
                    dname[strpos++] = c;
                }
            }
            pos += labellen;
            labellen = src[pos++];
            dname[strpos++] = '.';
        }
    }
    dname[strpos] = '\0';
}

void dns_pktinfo2str(char* dest, dns_pkt_info_t* dns_pkt_info, size_t max_len) {
    uint32_t ttl;
    char dname[800];
    char ip[INET6_ADDRSTRLEN];
    memset(dname, 0, 800);

    ttl = dns_pkt_info->ttl;
    if (dns_pkt_info->family == AF_INET) {
        ntop(AF_INET, ip, dns_pkt_info->ip + 12, INET6_ADDRSTRLEN);
    } else {
        ntop(AF_INET6, ip, dns_pkt_info->ip, INET6_ADDRSTRLEN);
    }

    dns_dname2str(dname, dns_pkt_info->dname, 800);
    snprintf(dest, max_len,
             "%s %s %u", ip, dname, ttl);
}

static inline void write_int16(uint8_t* dest, uint16_t i) {
    const uint16_t wi = htons(i);
    memcpy(dest, &wi, 2);
}

static inline void write_int32(uint8_t* dest, uint32_t i) {
    const uint32_t wi = htonl(i);
    memcpy(dest, &wi, 4);
}

static inline void write_int64(uint8_t* dest, uint64_t i) {
    uint64_t wi;
    if (htonl(123) != 123) {
        wi = htobe64(i);
    }
    memcpy(dest, &wi, 8);
}

static inline uint16_t read_int16(uint8_t* src) {
    uint16_t wi;
    memcpy(&wi, src, 2);
    return ntohs(wi);
}

static inline uint32_t read_int32(uint8_t* src) {
    uint32_t wi;
    memcpy(&wi, src, 4);
    return ntohl(wi);
}

static inline uint64_t read_int64(uint8_t* src) {
    uint64_t wi;
    memcpy(&wi, src, 8);
    if (htonl(123) != 123) {
        return be64toh(wi);
    } else {
        return wi;
    }
}


int pkt_info_equal(pkt_info_t* a, pkt_info_t* b) {
    return (memcmp(a, b, 39) == 0);
}

