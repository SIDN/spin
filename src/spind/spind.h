#ifndef SPIND_H
#define SPIND_H 1

// declaration of the functions commonly called by the registered
// modules.
//
// Note: if functions are needed here, it might mean that they should
// really be moved to the library or at least a separate file
#include "node_cache.h"

void maybe_sendflow(flow_list_t *flow_list, time_t now);
void report_block(int af, int proto, uint8_t *src_addr, uint8_t *dest_addr, unsigned src_port, unsigned dest_port, int payloadsize);

// Move these to, say, ip_list.[ch]
int ip_in_ignore_list(ip_t* ip);
int addr_in_ignore_list(int family, uint8_t* addr);
void send_command_dnsquery(dns_pkt_info_t* pkt_info);

// RPC section
int spinrpc_blockflow(int node1, int node2, int block);

#endif
