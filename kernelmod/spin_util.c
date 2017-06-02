
#include "spin_util.h"

ip_store_t* ip_store_create() {
	ip_store_t* ip_store = (ip_store_t*) kmalloc(sizeof(ip_store_t), __GFP_WAIT);
	ip_store->tree = (struct btree_head128*) kmalloc(sizeof(struct btree_head128), __GFP_WAIT);
	btree_init128(ip_store->tree);

	return ip_store;
}

void ip_store_destroy(ip_store_t* ip_store) {
	btree_destroy128(ip_store->tree);
	kfree(ip_store->tree);
	kfree(ip_store);
}

static inline void split_key(unsigned char ip[16], u64* k1, u64* k2) {
	memcpy(k1, ip, 8);
	memcpy(k2, ip+8, 8);
}

int ip_store_contains_ip(ip_store_t* ip_store, unsigned char ip[16]) {
	u64 k1, k2;
	split_key(ip, &k1, &k2);
	return (btree_lookup128(ip_store->tree, k1, k2) != NULL);
}

void ip_store_add_ip(ip_store_t* ip_store, unsigned char ip[16]) {
	u64 k1, k2;
	if (!ip_store_contains_ip(ip_store, ip)) {
		split_key(ip, &k1, &k2);
		// Note, we do not use the data, we only misuse btree for a key list
		// but we do need some pointer as a value. Just use the store itself
		btree_insert128(ip_store->tree, k1, k2, ip_store, __GFP_WAIT);
	}
}

void ip_store_remove_ip(ip_store_t* ip_store, unsigned char ip[16]) {
	u64 k1, k2;
	if (ip_store_contains_ip(ip_store, ip)) {
		split_key(ip, &k1, &k2);
		btree_remove128(ip_store->tree, k1, k2);
	}
}

void ip_store_for_each(ip_store_t* ip_store,
					   void(*cb)(unsigned char[16], void* data),
					   void* data) {
	u64 k1, k2;
	void* val = NULL;
	unsigned char ip[16];
	btree_for_each_safe128(ip_store->tree, k1, k2, val) {
		memcpy(ip, &k1, 8);
		memcpy(ip+8, &k2, 8);
		cb(ip, data);
	}
}
