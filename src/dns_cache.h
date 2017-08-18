
#ifndef DNS_CACHE_H
#define DNS_CACHE_H 1

#include "pkt_info.h"

#include "tree.h"

/**
 * cache of dns requests
 */

#define MAX_SIZE 8000

typedef struct dns_cache_entry_s {
    // this is the domain(string)->expiry (timestamp + ttl, uint32_t) mapping
    tree_t* domains;
} dns_cache_entry_t;

// todo: make this a tree or dict
typedef struct {
    // this tree maps ip's to trees that map domain names to expiry timestamps
    // 192.0.2.1
    //       |- example.com 123451245
    //       |- example.net 123461234
    tree_t* entries;
} dns_cache_t;


dns_cache_entry_t* dns_cache_entry_create();
void dns_cache_entry_destroy(dns_cache_entry_t* dns_cache_entry);

void dns_cache_entry_print(dns_cache_entry_t* entry);


dns_cache_t* dns_cache_create();
void dns_cache_destroy(dns_cache_t* dns_cache);

// note: this copies the data
void dns_cache_add(dns_cache_t* cache, dns_pkt_info_t* dns_pkt_info, uint32_t timestamp);
void dns_cache_clean(dns_cache_t* dns_cache, uint32_t now);
void dns_cache_print(dns_cache_t* dns_cache);

dns_cache_entry_t* dns_cache_find(dns_cache_t* dns_cache, uint8_t ip[17]);

#endif // DNS_CACHE_H
