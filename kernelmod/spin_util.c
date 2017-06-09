
#include "spin_util.h"

char* ipv4_val = "4";
char* ipv6_val = "6";

ip_store_t* ip_store_create() {
	ip_store_t* ip_store = (ip_store_t*) kmalloc(sizeof(ip_store_t), __GFP_WAIT);
	ip_store->elements = NULL;

	return ip_store;
}

void ip_store_destroy(ip_store_t* ip_store) {
	ip_store_el_t* el = ip_store->elements;
	while (el != NULL) {
		el = el->next;
		kfree(el);
	}
	kfree(ip_store);
}

static inline void split_key(unsigned char ip[16], u64* k1, u64* k2) {
	memcpy(k1, ip, 8);
	memcpy(k2, ip+8, 8);
}

int ip_store_contains_ip(ip_store_t* ip_store, unsigned char ip[16]) {
	u64 k1, k2;
	ip_store_el_t* el = ip_store->elements;
	split_key(ip, &k1, &k2);
	while (el != NULL) {
		if (k1 == el->k1 && k2 == el->k2) {
			return 1;
		}
		el = el->next;
	}
	return 0;
}

void ip_store_add_ip(ip_store_t* ip_store, int ipv6, unsigned char ip[16]) {
	u64 k1, k2;
	char* ipv = ipv6 ? ipv6_val : ipv4_val;
	ip_store_el_t* el;
	ip_store_el_t* el_new;

	if (!ip_store_contains_ip(ip_store, ip)) {
		split_key(ip, &k1, &k2);
		el_new = (ip_store_el_t*)kmalloc(sizeof(ip_store_el_t), __GFP_WAIT);
		el_new->k1 = k1;
		el_new->k2 = k2;
		el_new->val = ipv;
		el_new->next = NULL;
		el = ip_store->elements;
		if (el == NULL) {
			ip_store->elements = el_new;
		} else {
			while (el->next != NULL) {
				el = el->next;
			}
			el->next = el_new;
		}
	}
}

void ip_store_remove_ip(ip_store_t* ip_store, unsigned char ip[16]) {
	u64 k1, k2;
	ip_store_el_t* prev_el;
	ip_store_el_t* el = ip_store->elements;

	if (el == NULL) {
		return;
	}
	split_key(ip, &k1, &k2);
	if (el->k1 == k1 && el->k2 == k2) {
		ip_store->elements = el->next;
		kfree(el);
		return;
	}
	prev_el = el;
	el = el->next;
	while (el != NULL) {
		if (el->k1 == k1 && el->k2 == k2) {
			prev_el->next = el->next;
			kfree(el);
			return;
		}
		prev_el = el;
		el = el->next;
	}
	return;
}

void ip_store_for_each(ip_store_t* ip_store,
					   void(*cb)(unsigned char[16], int, void* data),
					   void* data) {
	unsigned char ip[16];
	ip_store_el_t* el = ip_store->elements;

	while (el != NULL) {
		printk("[XX] cur el: %p\n", el);
		memcpy(ip, &el->k1, 8);
		memcpy(ip+8, &el->k2, 8);
		cb(ip, el->val == ipv6_val, data);
		el = el->next;
	}
}
