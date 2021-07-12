#include <err.h>

#include "extsrc.h"
#include "spin_log.h"

uint8_t
extsrc_af_to_wire(uint8_t af)
{
    switch (af) {
    case AF_INET:
        return EXTSRC_AF_INET;
    case AF_INET6:
        return EXTSRC_AF_INET6;
    default:
        spin_log(LOG_WARNING, "%s: unknown value %d\n", __func__, af);
        return af; // XXX
    }
}

uint8_t
extsrc_af_from_wire(uint8_t x)
{
    switch (x) {
    case EXTSRC_AF_INET:
        return AF_INET;
    case EXTSRC_AF_INET6:
        return AF_INET6;
    default:
        spin_log(LOG_WARNING, "%s: unknown value %d\n", __func__, x);
        return x; // XXX
    }
}

static void *
extsrc_msg_payload(struct extsrc_msg *msg)
{
    return msg->data + sizeof(struct extsrc_msg_hdr);
}

/******************************************************************************/

struct extsrc_msg *
extsrc_msg_create(char *payload, uint32_t payload_len, uint32_t msg_type)
{
    struct extsrc_msg *msg;
    struct extsrc_msg_hdr hdr;

    hdr.type = msg_type;
    hdr.length = payload_len;

    msg = malloc(sizeof(struct extsrc_msg));
    if (!msg) {
        err(1, "malloc");
    }

    msg->length = sizeof(hdr) + payload_len;
    msg->data = malloc(msg->length);
    if (!msg->data) {
        err(1, "malloc");
    }

    memcpy(msg->data, &hdr, sizeof(hdr));
    memcpy(msg->data + sizeof(hdr), payload, payload_len);

    return msg;
}

struct extsrc_msg *
extsrc_msg_create_pkt_info(pkt_info_t *pkt)
{
    struct extsrc_msg *res = extsrc_msg_create((char *)pkt, sizeof(*pkt),
        EXTSRC_MSG_TYPE_PKT_INFO);

    ((pkt_info_t *)extsrc_msg_payload(res))->family = extsrc_af_to_wire(pkt->family);

    return res;
}

struct extsrc_msg *
extsrc_msg_create_dns_query(dns_pkt_info_t *dns_pkt, int family,
    uint8_t *src_addr)
{
    struct extsrc_dns_query_hdr hdr;
    char *buf;
    size_t buf_len;
    struct extsrc_msg *msg;

    hdr.family = family;
    memcpy(hdr.src_addr, src_addr, sizeof(hdr.src_addr));

    buf_len = sizeof(hdr) + sizeof(*dns_pkt);
    buf = malloc(buf_len);
    if (!buf) {
        err(1, "malloc");
    }

    memcpy(buf, &hdr, sizeof(hdr));
    memcpy(buf + sizeof(hdr), dns_pkt, sizeof(*dns_pkt));

    msg = extsrc_msg_create(buf, buf_len, EXTSRC_MSG_TYPE_DNS_QUERY);

    free(buf);

    return msg;
}

struct extsrc_msg *
extsrc_msg_create_dns_answer(dns_pkt_info_t *dns_pkt)
{
    return extsrc_msg_create((char *)dns_pkt, sizeof(*dns_pkt),
        EXTSRC_MSG_TYPE_DNS_ANSWER);
}

struct extsrc_msg *
extsrc_msg_create_arp_table_update(struct extsrc_arp_table_update *up)
{
    return extsrc_msg_create((char *)up, sizeof(*up),
        EXTSRC_MSG_TYPE_ARP_TABLE_UPDATE);
}

void
extsrc_msg_free(struct extsrc_msg *msg)
{
    free(msg->data);
    free(msg);
}
