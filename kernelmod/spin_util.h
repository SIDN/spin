/**
 * Various utility structure and functions for the spin kernel module
 */

#ifndef SPIN_UTIL_H
#define SPIN_UTIL_H 1

#include <linux/btree.h>
#include <linux/cache.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>

typedef struct  {
	struct btree_head128* tree;
} ip_store_t;

ip_store_t* ip_store_create(void);

void ip_store_destroy(ip_store_t* ip_store);

int ip_store_contains_ip(ip_store_t* ip_store, unsigned char ip[16]);

void ip_store_add_ip(ip_store_t* ip_store, int ipv6, unsigned char ip[16]);

void ip_store_remove_ip(ip_store_t* ip_store, unsigned char ip[16]);

void ip_store_for_each(ip_store_t* ip_store,
					   void(*cb)(unsigned char[16], int is_ipv6, void* data),
					   void* data);

#ifndef __GFP_WAIT
#define __GFP_WAIT __GFP_RECLAIM
#endif

#endif // SPIN_UTIL_H
