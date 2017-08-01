
#include "dns_cache.h"
#include <stdlib.h>

#include <assert.h>

#include "tree.h"

int cmp_ip(size_t size_a, void* key_a, size_t size_b, void* key_b) {
    assert(size_a == 17);
    assert(size_b == 17);
    return memcmp(key_a, key_b, 17);
}

// note, this does not take label order into account, it is just on pure bytes
int cmp_domains(size_t size_a, void* a, size_t size_b, void* b) {
    size_t s = size_a;
    int result;

    if (s > size_b) {
        s = size_b;
    }
    result = memcmp(a, b, s);
    if (result == 0) {
        if (size_a > size_b) {
            return -1;
        } else if (size_a < size_b) {
            return 1;
        } else {
            return 0;
        }
    }
    return result;
}

dns_cache_entry_t*
dns_cache_entry_create() {
    dns_cache_entry_t* entry = (dns_cache_entry_t*) malloc(sizeof(dns_cache_entry_t));
    entry->domains = tree_create(cmp_domains);
    return entry;
}

void
dns_cache_entry_destroy(dns_cache_entry_t* entry) {
    tree_destroy(entry->domains);
    printf("[XX] free entry at %p\n", entry);
    free(entry);
}

void
dns_cache_entry_print(dns_cache_entry_t* entry) {
    tree_entry_t* cur = tree_first(entry->domains);
    while (cur != NULL) {
        printf("    %s\n", cur->key);
        cur = tree_next(cur);
    }
}

dns_cache_t*
dns_cache_create() {
    dns_cache_t* dns_cache = (dns_cache_t*) malloc(sizeof(dns_cache_t));
    dns_cache->entries = tree_create(cmp_ip);

    return dns_cache;
}

void dns_cache_add(dns_cache_t* cache, dns_pkt_info_t* dns_pkt_info) {
    tree_entry_t* t_entry = tree_find(cache->entries, 17, dns_pkt_info);
    dns_cache_entry_t* entry;
    unsigned char dname[1024];
    dns_dname2str(dname, dns_pkt_info->dname, 1024);

    if (t_entry == NULL) {
        entry = dns_cache_entry_create();
        printf("[XX] create new IP entry for %s (%p)\n", dname, entry);
        printf("[XX] created entry at %p\n", entry);
        tree_add(entry->domains, strlen(dname)+1, dname, sizeof(dns_pkt_info->ttl), &dns_pkt_info->ttl, 1);
        // todo, make noncopy?
        tree_add(cache->entries, 17, dns_pkt_info, sizeof(entry), entry, 1);
        free(entry);
    } else {
        entry = (dns_cache_entry_t*)t_entry->data;
        printf("[XX] add domain %s to existing IP entry (%p)\n", dname, entry);
        tree_add(entry->domains, strlen(dname)+1, dname, sizeof(dns_pkt_info->ttl), &dns_pkt_info->ttl, 1);
    }
}

void
dns_cache_destroy(dns_cache_t* dns_cache) {
    dns_cache_entry_t* entry;
    tree_entry_t* cur = tree_first(dns_cache->entries);
    while (cur != NULL) {
        //nxt = tree_next(cur);
        printf("[XX] DESTROY cache entry at %p\n", cur);
        printf("[XX] DATA of cache entry at %p\n", cur->data);
        entry = (dns_cache_entry_t*)cur->data;
        dns_cache_entry_destroy(entry);

        cur->data = NULL;
        cur = tree_next(cur);
    }

    tree_destroy(dns_cache->entries);
    free(dns_cache);
}

void
dns_cache_print(dns_cache_t* dns_cache) {
    char str[1024];
    unsigned char* keyp;
    int fam;
    dns_cache_entry_t* entry;
    tree_entry_t* cur, * cur_domain;

    cur = tree_first(dns_cache->entries);
    while (cur != NULL) {
        printf("entry...\n");
        keyp = (unsigned char*)cur->key;
        fam = (int)keyp[0];
        if (fam == AF_INET) {
            ntop((int)keyp[0], str, (unsigned char*)&keyp[13], 1024);
        } else {
            ntop((int)keyp[0], str, (unsigned char*)&keyp[1], 1024);
        }
        printf("[CACHE] '%s'\n", str);
        entry = (dns_cache_entry_t*)cur->data;
        cur_domain = tree_first(entry->domains);
        while (cur_domain != NULL) {
            printf("    [domain] '%s' (%p)\n", cur_domain->key, cur_domain->key);
            cur_domain = tree_next(cur_domain);
        }

        cur = tree_next(cur);
    }
}
