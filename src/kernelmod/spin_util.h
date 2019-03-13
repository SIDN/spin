/**
 * Various utility structure and functions for the spin kernel module
 */

#ifndef SPIN_UTIL_H
#define SPIN_UTIL_H 1

//#include <linux/cache.h>
//#include <linux/kernel.h>
//#include <linux/slab.h>
//#include <linux/module.h>

#include "pkt_info.h"

typedef struct ip_store_el {
	u64 k1;
	u64 k2;
	char* val;
	struct ip_store_el* next;
} ip_store_el_t;

typedef struct  {
	ip_store_el_t* elements;
} ip_store_t;

ip_store_t* ip_store_create(void);
void ip_store_destroy(ip_store_t* ip_store);
int ip_store_contains_ip(ip_store_t* ip_store, unsigned char ip[16]);
void ip_store_add_ip(ip_store_t* ip_store, int ipv6, unsigned char ip[16]);
void ip_store_remove_ip(ip_store_t* ip_store, unsigned char ip[16]);
void ip_store_for_each(ip_store_t* ip_store,
                       void(*cb)(unsigned char[16], int is_ipv6, void* data),
                       void* data);

void log_set_verbosity(int new_verbosity);
int log_get_verbosity(void);
int* log_get_verbosity_ptr(void);
void log_packet(pkt_info_t* pkt_info);
void printv(int module_verbosity, const char* format, ...);
void hexdump_k(uint8_t* data, unsigned int offset, unsigned int size);


#ifndef __GFP_WAIT
#define __GFP_WAIT __GFP_RECLAIM
#endif

#endif // SPIN_UTIL_H
