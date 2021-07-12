#ifndef INCLUDE_EXTSRC_H
#define INCLUDE_EXTSRC_H 1

#include <stdint.h>

#include "dns_cache.h"
#include "node_cache.h"

#define EXTSRC_SOCKET_PATH "/var/run/spin-extsrc.sock"

#define EXTSRC_PORT 20867

/*
 * Before sending an extsrc_msg over the network, convert any address family
 * values to these defines and convert them back when receiving them.
 */
#define EXTSRC_AF_INET 4
#define EXTSRC_AF_INET6 6

uint8_t extsrc_af_to_wire(uint8_t);
uint8_t extsrc_af_from_wire(uint8_t);

/******************************************************************************/

/*
 * MESSAGE TYPE-SPECIFIC #define'S AND STRUCTURES
 */

/*
 * Message types
 */
/* Payload consists of: pkt_info_t */
#define EXTSRC_MSG_TYPE_PKT_INFO 1
/* Payload consists of: struct dns_query_hdr followed by dns_pkt_info_t */
#define EXTSRC_MSG_TYPE_DNS_QUERY 2
/* Payload consists of: dns_pkt_info_t */
#define EXTSRC_MSG_TYPE_DNS_ANSWER 3
/* Payload consists of: struct extsrc_arp_table_update */
#define EXTSRC_MSG_TYPE_ARP_TABLE_UPDATE 4

/*
 * Additional information for the MSG_TYPE_DNS_QUERY message type.
 */
struct extsrc_dns_query_hdr {
    int family;
    uint8_t src_addr[16];
};

#define ETHER_ADDR_STRLEN sizeof("aa:bb:cc:dd:ee:ff")
/*
 * Payload structure for the EXTSRC_MSG_TYPE_ARP_TABLE_UPDATE message type.
 * For a certain MAC address, it lists an IP address that should be added to the
 * ARP table.
 */
struct extsrc_arp_table_update {
    char mac[ETHER_ADDR_STRLEN];
    ip_t ip;
};
#undef ETHER_ADDR_STRLEN

/*
 * END OF MESSAGE TYPE-SPECIFIC #define'S AND STRUCTURES
 */

/******************************************************************************/

/*
 * Header that will be prepended to the payload.
 */
struct extsrc_msg_hdr {
    uint32_t type; /* Message type (see above) */
    uint32_t length; /* Length of the payload (i.e. without this header) */
};

/*
 * Structure that will be returned to callers of the extsrc_msg_create*()
 * functions. Can be freed with extsrc_msg_free();
 */
struct extsrc_msg {
    char *data; /* The actual data that will be sent through the socket */
    size_t length; /* The length of the data in bytes */
};

/*
 * The extsrc_msg_create*() functions create messages that can be sent to the
 * socket. These functions either succeed or terminates the program so there is
 * no need to check for failure. The caller of these function is responsible for
 * freeing the result using the extsrc_msg_free() function.
 *
 * Parameters (only extsrc_msg_create()):
 *  payload: pointer to the payload that should be put in the message.
 *  payload_len: length of the payload pointed to by the payload pointer.
 *  msg_type: type of the to be created message.
 *
 * Parameters (other functions):
 *  <parameters specific to the message type>
 *
 * Generally, there is no need to use the extsrc_msg_create(); instead, the
 * function for a specific message type should be used.
 */
struct extsrc_msg *extsrc_msg_create(char *payload, uint32_t payload_len,
    uint32_t msg_type);
struct extsrc_msg *extsrc_msg_create_pkt_info(pkt_info_t *pkt);
struct extsrc_msg *extsrc_msg_create_dns_query(dns_pkt_info_t *dns_pkt,
    int family, uint8_t *src_addr);
struct extsrc_msg *extsrc_msg_create_dns_answer(dns_pkt_info_t *dns_pkt);
struct extsrc_msg
    *extsrc_msg_create_arp_table_update(struct extsrc_arp_table_update *up);

void extsrc_msg_free(struct extsrc_msg *msg);

#endif
