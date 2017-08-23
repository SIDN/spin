
#include "dns_cache.h"
#include "util.h"

#include <assert.h>

#include <time.h>

// returns true if the given domain is one of the domains for the
// given ip in the cache
// if domain is NULL, the ip should not be in the cache
void
check_ip_domainname(dns_cache_t* dns_cache, const char* ip_str, const char* domain) {
    ip_t ip;
    assert(spin_pton(&ip, ip_str));
    tree_entry_t* domain_entry;

    dns_cache_entry_t* entry = dns_cache_find(dns_cache, &ip);
    if (domain == NULL) {
        if (entry != NULL) {
            printf("Domain NULL but entry found for %s\n", ip_str);
            assert(0);
        }
    } else {
        if (entry == NULL) {
            printf("IP %s not found in cache\n", ip_str);
            assert(0);
        }
        domain_entry = tree_find(entry->domains, strlen(domain) + 1, domain);
        printf("[XX] DNS ENTRY:\n");
        dns_cache_entry_print(entry);
        printf("[XX] END OF DNS ENTRY\n");
        if (domain_entry == NULL) {
            printf("IP %s found but domain %s not cached for it\n", ip_str, domain);
            assert(0);
        }
        printf("[XX] found %s in dns cache entry (one of %d domains for IP %s\n", domain, tree_size(entry->domains), ip_str);
    }
}

void
check_ip_domain_ttl(dns_cache_t* dns_cache, const char* ip_str, const char* domain, uint32_t ttl) {
    ip_t ip;
    assert(spin_pton(&ip, ip_str));
    tree_entry_t* domain_entry;
    dns_cache_entry_t* entry = dns_cache_find(dns_cache, &ip);
    uint32_t* found_ttl;

    if (entry == NULL) {
        printf("No entry found for %s\n", ip_str);
        assert(0);
    }
    domain_entry = tree_find(entry->domains, strlen(domain) + 1, domain);
    if (domain_entry == NULL) {
        printf("Domain %s not found in dns cache entry for %s\n", domain, ip_str);
        assert(0);
    }
    found_ttl = (uint32_t*)domain_entry->data;
    if (*found_ttl != ttl) {
        printf("TTL for domain %s in entry for IP %s is %u, but expected %u\n", domain, ip_str, *found_ttl, ttl);
        assert(0);
    }
}

void
sample_dns_pkt_info_1(dns_pkt_info_t* dns_pkt_info) {
    message_type_t mt;
    char wire[] = { 0x01, 0x02, 0x00, 0x00, // header, dns packet
                    0x02, // ipv4
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0xc0, 0x00, 0x02, 0x01, // 192.0.2.1
                    0x00, 0x00, 0x0e, 0x10, 0x0d, // TTL (3600)
                    0x03, 0x77, 0x77, 0x77, 0x04, 0x74, 0x65,
                    0x73, 0x74, 0x02, 0x6e, 0x6c, 0x00  // www.test.nl
                  };
    mt = wire2dns_pktinfo(dns_pkt_info, wire);
    printf("[XX] message type: %d\n", mt);
}

void
sample_dns_pkt_info_2(dns_pkt_info_t* dns_pkt_info) {
    message_type_t mt;
    char wire[] = { 0x01, 0x02, 0x00, 0x00, // header, dns packet
                    0x02, // ipv4
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0xc0, 0x00, 0x02, 0x02, // 192.0.2.2
                    0x00, 0x00, 0x0e, 0x10, // TTL (3600)
                    0x0d, // 13 octets
                    0x03, 0x77, 0x77, 0x77, 0x04, 0x74, 0x65,
                    0x73, 0x74, 0x02, 0x6e, 0x6c, 0x00  // www.test.nl
                  };
    mt = wire2dns_pktinfo(dns_pkt_info, wire);
    printf("[XX] message type: %d\n", mt);
}

void
sample_dns_pkt_info_3(dns_pkt_info_t* dns_pkt_info) {
    message_type_t mt;
    char wire[] = { 0x01, 0x02, 0x00, 0x00, // header, dns packet
                    0x02, // ipv4
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0xc0, 0x00, 0x02, 0x01, // 192.0.2.2
                    0x00, 0x00, 0x0e, 0x10, // TTL (3600)
                    0x0e, // 14 octets
                    0x03, 0x77, 0x77, 0x77, 0x05, 0x74, 0x65,
                    0x73, 0x74, 0x32, 0x02, 0x6e, 0x6c, 0x00  // www.test2.nl
                  };
    mt = wire2dns_pktinfo(dns_pkt_info, wire);
    printf("[XX] message type: %d\n", mt);
}

void
test_dns_cache_add() {
    dns_pkt_info_t dns_pkt_info;

    dns_cache_t* dns_cache = dns_cache_create();

    sample_dns_pkt_info_1(&dns_pkt_info);

    assert(tree_size(dns_cache->entries) == 0);
    check_ip_domainname(dns_cache, "192.0.2.1", NULL);

    dns_cache_add(dns_cache, &dns_pkt_info, 12345);

    assert(tree_size(dns_cache->entries) == 1);
    check_ip_domainname(dns_cache, "192.0.2.1", "www.test.nl.");

    // adding it again should have no effect on size
    dns_cache_add(dns_cache, &dns_pkt_info, 12345);
    assert(tree_size(dns_cache->entries) == 1);
    check_ip_domainname(dns_cache, "192.0.2.1", "www.test.nl.");

    // adding another one should
    sample_dns_pkt_info_2(&dns_pkt_info);
    dns_cache_add(dns_cache, &dns_pkt_info, 12345);
    assert(tree_size(dns_cache->entries) == 2);
    check_ip_domainname(dns_cache, "192.0.2.1", "www.test.nl.");
    check_ip_domainname(dns_cache, "192.0.2.2", "www.test.nl.");

    dns_cache_destroy(dns_cache);
}

void
test_dns_cache_add_same_ip() {
    dns_pkt_info_t dns_pkt_info;

    dns_cache_t* dns_cache = dns_cache_create();

    sample_dns_pkt_info_1(&dns_pkt_info);

    assert(tree_size(dns_cache->entries) == 0);

    dns_cache_add(dns_cache, &dns_pkt_info, 12345);

    assert(tree_size(dns_cache->entries) == 1);
    check_ip_domainname(dns_cache, "192.0.2.1", "www.test.nl.");

    // adding another one should
    sample_dns_pkt_info_3(&dns_pkt_info);
    dns_cache_add(dns_cache, &dns_pkt_info, 12345);
    assert(tree_size(dns_cache->entries) == 1);
    check_ip_domainname(dns_cache, "192.0.2.1", "www.test.nl.");
    check_ip_domainname(dns_cache, "192.0.2.1", "www.test2.nl.");

    dns_cache_destroy(dns_cache);
}

void
test_dns_cache_clean() {
    dns_pkt_info_t dns_pkt_info1;
    dns_pkt_info_t dns_pkt_info2;

    dns_cache_t* dns_cache = dns_cache_create();

    sample_dns_pkt_info_1(&dns_pkt_info1);
    sample_dns_pkt_info_2(&dns_pkt_info2);

    dns_cache_add(dns_cache, &dns_pkt_info1, 10000);
    dns_cache_add(dns_cache, &dns_pkt_info2, 20000);
    assert(tree_size(dns_cache->entries) == 2);

    // this should keep both
    dns_cache_clean(dns_cache, 100);
    assert(tree_size(dns_cache->entries) == 2);

    // this should remove both
    dns_cache_clean(dns_cache, 25000);
    assert(tree_size(dns_cache->entries) == 0);

    // add again and now remove just one
    dns_cache_add(dns_cache, &dns_pkt_info1, 10000);
    dns_cache_add(dns_cache, &dns_pkt_info2, 20000);
    assert(tree_size(dns_cache->entries) == 2);
    dns_cache_clean(dns_cache, 15000);
    assert(tree_size(dns_cache->entries) == 1);

    dns_cache_destroy(dns_cache);
}

void
test_dns_cache_overwrite_ttl() {
    dns_pkt_info_t dns_pkt_info1;

    dns_cache_t* dns_cache = dns_cache_create();

    sample_dns_pkt_info_1(&dns_pkt_info1);

    dns_cache_add(dns_cache, &dns_pkt_info1, 10000);
    assert(tree_size(dns_cache->entries) == 1);

    check_ip_domain_ttl(dns_cache, "192.0.2.1", "www.test.nl.", 13600);

    dns_pkt_info1.ttl = 7200;
    dns_cache_add(dns_cache, &dns_pkt_info1, 10000);
    check_ip_domain_ttl(dns_cache, "192.0.2.1", "www.test.nl.", 17200);

    dns_cache_destroy(dns_cache);
}

int
main(int argc, char** argv) {
    test_dns_cache_add();
    test_dns_cache_add_same_ip();
    test_dns_cache_clean();
    test_dns_cache_overwrite_ttl();
    return 0;
}
