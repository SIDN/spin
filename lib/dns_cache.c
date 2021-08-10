
#include <time.h>

#include "dns_cache.h"
#include "spin_log.h"


dns_cache_entry_t*
dns_cache_entry_create() {
    dns_cache_entry_t* entry = (dns_cache_entry_t*) malloc(sizeof(dns_cache_entry_t));
    entry->domains = tree_create(cmp_domains);
    return entry;
}

void
dns_cache_entry_destroy(dns_cache_entry_t* entry) {
    tree_destroy(entry->domains);
    free(entry);
}

void
dns_cache_entry_print(dns_cache_entry_t* entry) {
    tree_entry_t* cur = tree_first(entry->domains);
    while (cur != NULL) {
        spin_log(LOG_DEBUG, "    %s\n", (char*)cur->key);
        cur = tree_next(cur);
    }
}

dns_cache_t*
dns_cache_create() {
    dns_cache_t* dns_cache = (dns_cache_t*) malloc(sizeof(dns_cache_t));
    dns_cache->entries = tree_create(cmp_ipdata_raw);

    return dns_cache;
}

// Add name and ip directly (ie. we wrap in dns_pkt_info ourselves
void dns_cache_add_dname_ip(dns_cache_t* cache, uint8_t family, uint32_t ttl, char* dname, const ip_t* ip, uint32_t timestamp) {
    dns_pkt_info_t* dns_pkt_info = (dns_pkt_info_t*)malloc(sizeof(dns_pkt_info_t));
    dns_pkt_info->family = family;
    memcpy(dns_pkt_info->ip, ip, sizeof(ip_t));
    strcpy(dns_pkt_info->dname, dname);
    dns_pkt_info->ttl = ttl;

    tree_entry_t* t_entry = tree_find(cache->entries, sizeof(ip_t), dns_pkt_info);
    dns_cache_entry_t* entry;

    timestamp += dns_pkt_info->ttl;

    if (t_entry == NULL) {
        entry = dns_cache_entry_create();
        tree_add(entry->domains, strlen(dname)+1, dname, sizeof(timestamp), &timestamp, 1);
        // todo, make noncopy?
        tree_add(cache->entries, sizeof(ip_t), dns_pkt_info, sizeof(entry), entry, 1);
        // free the outer shell but not the inner data
        //free(entry->domains);
        free(entry);
        //dns_cache_entry_destroy(entry);
    } else {
        entry = (dns_cache_entry_t*)t_entry->data;
        tree_add(entry->domains, strlen(dname)+1, dname, sizeof(timestamp), &timestamp, 1);
    }
}

void
dns_cache_add(dns_cache_t* cache, dns_pkt_info_t* dns_pkt_info, uint32_t timestamp) {
    tree_entry_t* t_entry = tree_find(cache->entries, 16, dns_pkt_info->ip);
    dns_cache_entry_t* entry;
    char dname[512];
    dns_dname2str(dname, (char*)dns_pkt_info->dname, 512);

    timestamp += dns_pkt_info->ttl;

    if (t_entry == NULL) {
        entry = dns_cache_entry_create();
        tree_add(entry->domains, strlen(dname)+1, dname, sizeof(timestamp), &timestamp, 1);
        // todo, make noncopy?
        tree_add(cache->entries, 16, dns_pkt_info->ip, sizeof(entry), entry, 1);
        // free the outer shell but not the inner data
        //free(entry->domains);
        free(entry);
        //dns_cache_entry_destroy(entry);
    } else {
        entry = (dns_cache_entry_t*)t_entry->data;
        tree_add(entry->domains, strlen(dname)+1, dname, sizeof(timestamp), &timestamp, 1);
    }
}

// Unused ??
void
dns_cache_destroy(dns_cache_t* dns_cache) {
    dns_cache_entry_t* entry;
    tree_entry_t* cur = tree_first(dns_cache->entries);
    while (cur != NULL) {
        // the entry's data was allocated separately upon addition
        // to the cache, so it needs to be destroyed too
        entry = (dns_cache_entry_t*)cur->data;
        dns_cache_entry_destroy(entry);

        cur->data = NULL;
        cur = tree_next(cur);
    }

    tree_destroy(dns_cache->entries);
    free(dns_cache);
}

void
dns_cache_clean(dns_cache_t* dns_cache, size_t clean_early) {
    uint32_t* expiry;
    dns_cache_entry_t* cur_dns;
    tree_entry_t* cur = tree_first(dns_cache->entries);
    tree_entry_t* nxt;
    tree_entry_t* cur_domain;
    tree_entry_t* nxt_domain;
    time_t now;

    time(&now);
    while (cur != NULL) {
        cur_dns = (dns_cache_entry_t*) cur->data;
        cur_domain = tree_first(cur_dns->domains);
        while (cur_domain != NULL) {
            nxt_domain = tree_next(cur_domain);
            expiry = (uint32_t*) cur_domain->data;
            if (clean_early > *expiry || (uint32_t)now > *expiry - clean_early) {
                tree_remove_entry(cur_dns->domains, cur_domain);
            }
            cur_domain = nxt_domain;
        }
        nxt = tree_next(cur);
        if (tree_empty(cur_dns->domains)) {
            // the entry's data was allocated separately upon addition
            // to the cache, so it needs to be destroyed too
            dns_cache_entry_destroy(cur_dns);
            cur->data = NULL;
            tree_remove_entry(dns_cache->entries, cur);
        }
        cur = nxt;
    }
}

void
dns_cache_print(dns_cache_t* dns_cache) {
    char str[1024];
    unsigned char* keyp;
    int fam;
    uint32_t* expiry;
    dns_cache_entry_t* entry;
    tree_entry_t* cur, * cur_domain;

    cur = tree_first(dns_cache->entries);
    while (cur != NULL) {
        keyp = (unsigned char*)cur->key;
        fam = (int)keyp[0];
        if (fam == AF_INET) {
            ntop((int)keyp[0], str, (unsigned char*)&keyp[13], 1024);
        } else {
            ntop((int)keyp[0], str, (unsigned char*)&keyp[1], 1024);
        }
        spin_log(LOG_DEBUG, "[IP] '%s'\n", str);
        entry = (dns_cache_entry_t*)cur->data;
        cur_domain = tree_first(entry->domains);
        while (cur_domain != NULL) {
            expiry = (uint32_t*) cur_domain->data;
            spin_log(LOG_DEBUG, "    [domain] '%s' (%u)\n", (char*)cur_domain->key, *expiry);
            cur_domain = tree_next(cur_domain);
        }

        cur = tree_next(cur);
    }
}

dns_cache_entry_t*
dns_cache_find(dns_cache_t* dns_cache, ip_t* ip) {
    tree_entry_t* entry = tree_find(dns_cache->entries, 16, ip->addr);
    if (entry) {
        return (dns_cache_entry_t*)entry->data;
    } else {
        return NULL;
    }
}
