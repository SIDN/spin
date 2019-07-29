#ifndef SPIN_DNS_H
#define SPIN_DNS_H 1

#include "pkt_info.h"

struct handle_dns_ctx;

struct handle_dns_ctx *handle_dns_init(void (*query_hook)(dns_pkt_info_t *, int, uint8_t *), void (*answer_hook)(dns_pkt_info_t *));
void handle_dns_cleanup(struct handle_dns_ctx* ctx);

void handle_dns_query(const struct handle_dns_ctx *ctx, const u_char *bp, u_int length, uint8_t* src_addr, int family);
void handle_dns_answer(const struct handle_dns_ctx *ctx, const u_char *bp, u_int length, int protocol);

#endif // SPIN_DNS_H
