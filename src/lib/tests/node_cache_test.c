
// Workaround for statistics code being too tightly coupled with spind at this moment
#include "disable_statistics.h"

#include "node_cache.h"
#include "pkt_info.h"
#include "test_helper.h"

#include <assert.h>

#include <time.h>

// Since most functions are now defined static, we include the source itself
#include "../node_cache.c"

node_t*
sample_node_1() {
    node_t* node = node_create(1);
    ip_t ip1;
    spin_pton(&ip1, "127.0.0.1");

    node_add_ip(node, &ip1);
    spin_pton(&ip1, "::1");
    node_add_ip(node, &ip1);
    node_add_domain(node, "tjeb.nl");
    node_add_domain(node, "example.nl");
    node_set_mac(node, "aa:bb:cc:dd:ee:ff");
    node_set_name(node, "sample node 1");
    return node;
}

node_t*
sample_node_2() {
    node_t* node = node_create(2);
    ip_t ip1;
    spin_pton(&ip1, "192.168.12.13");

    node_add_ip(node, &ip1);
    spin_pton(&ip1, "192.168.12.31");
    node_add_ip(node, &ip1);
    node_add_domain(node, "test.com.");
    node_add_domain(node, "test.nl");
    node_add_domain(node, "test.biz");
    node_set_mac(node, "ff:ff:ff:ff:ff:ff");
    node_set_name(node, "sample node 1");
    return node;
}

void
sample_pkt_info_1(pkt_info_t* pkt_info) {
    message_type_t mt;
    uint8_t wire[] = { 0x01, 0x01, 0x00, 0x00, 0x02, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0xa8, 0x08, 0x8d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc7, 0x10, 0x9c, 0x67, 0x83, 0x54, 0x01, 0xbb, 0x00, 0x04, 0x00, 0x00, 0x10, 0x1e, 0x00, 0x36 };
    mt = wire2pktinfo(pkt_info, wire);
    printf("[XX] message_type: %d\n", mt);
}

void
test_node_print() {
    node_t* node = sample_node_1();
    node_set_name(node, "new name for test");
    node_set_mac(node, "aa:aa:aa:aa:aa:aa");
    node_set_last_seen(node, 1);
    node_print(node);

    node_destroy(node);
}

void
test_node_shares_element() {
    node_t* node1 = sample_node_1();
    node_t* node2 = sample_node_2();
    node_t* node3 = node_create(3);

    ip_t ip;
    spin_pton(&ip, "127.0.0.1");

    assert(node_shares_element(node1, node2) == 0);
    assert(node_shares_element(node1, node3) == 0);
    assert(node_shares_element(node2, node3) == 0);

    node_add_ip(node3, &ip);
    assert(node_shares_element(node1, node2) == 0);
    assert(node_shares_element(node1, node3) == 1);
    assert(node_shares_element(node2, node3) == 0);

    node_add_domain(node3, "test.nl");
    assert(node_shares_element(node1, node2) == 0);
    assert(node_shares_element(node1, node3) == 1);
    assert(node_shares_element(node2, node3) == 1);

    node_destroy(node1);
    node_destroy(node2);
    node_destroy(node3);
}

void
sample_dns_pkt_info_1(dns_pkt_info_t* dns_pkt_info) {
    int mt;
    uint8_t wire[] = { 0x01, 0x02, 0x00, 0x00, // header
                       0x02, // AF_INET
                       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                       0x00, 0x00, 0x00, 0x00, 0x7f, 0x00, 0x00, 0x01, // IP (127.0.0.1)
                       0x00, 0x00, 0x00, 0x00, // ttl
                       0x0d, // dname size
                       0x03, 0x77, 0x77, 0x77, 0x04, 0x74, 0x6a, 0x65,
                       0x62, 0x02, 0x6e, 0x6c, 0x00 // dname (www.tjeb.nl)
                     };
    mt = wire2dns_pktinfo(dns_pkt_info, wire);
    assert(mt == 2);
}

void
sample_dns_pkt_info_2(dns_pkt_info_t* dns_pkt_info) {
    int mt;
    uint8_t wire[] = { 0x01, 0x02, 0x00, 0x00, // header
                       0x02, // AF_INET
                       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                       0x00, 0x00, 0x00, 0x00, 0x7f, 0x00, 0x00, 0x01, // IP (127.0.0.1)
                       0x00, 0x00, 0x00, 0x00, // ttl
                       0x0d, // dname size
                       0x03, 0x77, 0x77, 0x77, 0x04, 0x74, 0x6a, 0x61,
                       0x62, 0x02, 0x6e, 0x6c, 0x00 // dname (www.tjab.nl)
                     };
    mt = wire2dns_pktinfo(dns_pkt_info, wire);
    assert(mt == 2);
}

void
sample_dns_pkt_info_3(dns_pkt_info_t* dns_pkt_info) {
    int mt;
    uint8_t wire[] = { 0x01, 0x02, 0x00, 0x00, // header
                       0x02, // AF_INET
                       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                       0x00, 0x00, 0x00, 0x00, 0xc0, 0xa8, 0x01, 0x01, // IP (192.168.1.1)
                       0x00, 0x00, 0x00, 0x00, // ttl
                       0x0d, // dname size
                       0x03, 0x77, 0x77, 0x77, 0x04, 0x74, 0x6a, 0x61,
                       0x62, 0x02, 0x6e, 0x6c, 0x00 // dname (www.tjab.nl)
                     };
    mt = wire2dns_pktinfo(dns_pkt_info, wire);
    assert(mt == 2);
}

void
test_node_cache_add_1() {
    node_cache_t* node_cache = node_cache_create();
    //pkt_info_t pkt_info;
    dns_pkt_info_t info1, info2, info3;
    //char str[1024];

    sample_dns_pkt_info_1(&info1);
    sample_dns_pkt_info_2(&info2);
    sample_dns_pkt_info_3(&info3);

    node_cache_add_dns_info(node_cache, &info1, 12345);
    node_cache_add_dns_info(node_cache, &info2, 12345);
    node_cache_add_dns_info(node_cache, &info3, 12345);
    assert(tree_size(node_cache->nodes) == 1);

    node_cache_destroy(node_cache);
    node_cache = node_cache_create();

    node_cache_add_dns_info(node_cache, &info1, 12345);
    node_cache_add_dns_info(node_cache, &info3, 12345);
    // 1 and 3 share no data
    assert(tree_size(node_cache->nodes) == 2);

    // adding 2 now should merge 3 into it as well
    node_cache_add_dns_info(node_cache, &info2, 12345);
    assert(tree_size(node_cache->nodes) == 1);

    node_cache_destroy(node_cache);
}

void
test_node_cache_add_2() {
    node_cache_t* node_cache = node_cache_create();
    pkt_info_t pkt_info;
    sample_pkt_info_1(&pkt_info);
    node_cache_add_pkt_info(node_cache, &pkt_info, 12345);
    assert(tree_size(node_cache->nodes) == 2);

    node_cache_destroy(node_cache);
}

void
test_node_cache_add_3() {
    node_cache_t* node_cache = node_cache_create();
    node_t* node1 = node_create(1);
    node_t* f_node;
    ip_t ip;

    spin_pton(&ip, "192.0.2.1");
    node_add_domain(node1, "www.example.com");
    node_add_ip(node1, &ip);
    node_set_mac(node1, "aa:aa:aa:aa:aa:aa");
    node_set_name(node1, "foo bar");
    node_set_last_seen(node1, 12345);

    node_cache_add_node(node_cache, node_clone(node1));

    f_node = node_cache_find_by_ip(node_cache, 17, &ip);
    assert(f_node != NULL);
    assert(f_node->last_seen == 12345);
    assert(f_node->is_blocked == 0);
    assert(f_node->is_allowed == 0);

    node_set_last_seen(node1, 54321);
    node_cache_add_node(node_cache, node_clone(node1));

    f_node = node_cache_find_by_ip(node_cache, 17, &ip);
    assert(f_node != NULL);
    assert(f_node->last_seen == 54321);


    // check blocked and excepted statuses stay when merging a node that
    // has them at zero
    node_set_last_seen(node1, 54322);
    node_cache_add_node(node_cache, node_clone(node1));

    f_node = node_cache_find_by_ip(node_cache, 17, &ip);
    assert(f_node != NULL);
    assert(f_node->last_seen == 54322);

    node_destroy(node1);
    node_cache_destroy(node_cache);
}

void
test_node_to_json() {
    node_t* node1 = node_create(1);
    ip_t ip;
    buffer_t* json_buf = buffer_create(1024);
    int str_cmp;
    const char* expected_json = "{ \"id\": 1,  \"name\": \"foo bar\",  \"mac\": \"aa:aa:aa:aa:aa:aa\",  \"lastseen\": 12345,  \"ips\": [ \"192.0.2.1\", \"192.0.2.2\" ],  \"domains\": [ \"www.example.com\", \"www.example2.com\" ] }";

    node_add_domain(node1, "www.example.com");
    node_add_domain(node1, "www.example2.com");
    spin_pton(&ip, "192.0.2.1");
    node_add_ip(node1, &ip);
    spin_pton(&ip, "192.0.2.2");
    node_add_ip(node1, &ip);
    node_set_mac(node1, "aa:aa:aa:aa:aa:aa");
    node_set_name(node1, "foo bar");
    node_set_last_seen(node1, 12345);

    node2json(node1, json_buf);
    buffer_finish(json_buf);

    str_cmp = strcmp(buffer_str(json_buf), expected_json);

    assertf(str_cmp == 0, "Wrong JSON data; got:\n%s\nexpected:\n%s\n", buffer_str(json_buf), expected_json);

    buffer_destroy(json_buf);
    node_destroy(node1);
}

void
test_pkt_info_to_json() {
    node_cache_t* node_cache = node_cache_create();
    buffer_t* json_buf = buffer_create(1024);
    //int str_cmp;
    pkt_info_t pkt_info;
    size_t dest_size = 1024;
    char tmp[dest_size];
    uint32_t timestamp;

    sample_pkt_info_1(&pkt_info);

    node_cache_add_pkt_info(node_cache, &pkt_info, 11111);

    printf("[XX] NODE CACHE: \n");
    node_cache_print(node_cache);

    pktinfo2str(tmp, &pkt_info, dest_size);
    printf("[XX] PKTINFO: %s\n", tmp);

    pkt_info2json(node_cache, &pkt_info, json_buf);

    assert(buffer_finish(json_buf));

    printf("[XX] PKTINFO JSON: '%s'\n", buffer_str(json_buf));

    timestamp = time(NULL);
    (void)timestamp;
    // todo: list
    //create_traffic_command(node_cache, &pkt_info, dest, dest_size, timestamp);
    printf("[XX] FULL COMMAND (%lu): %s\n", buffer_size(json_buf), buffer_str(json_buf));

    buffer_destroy(json_buf);
    node_cache_destroy(node_cache);
}

void
test_node_cache_domains_case() {
    node_t* node = node_create(1);
    assert(tree_size(node->domains) == 0);
    node_add_domain(node, "aaaaa.nl");
    assert(tree_size(node->domains) == 1);
    node_add_domain(node, "bbbbb.nl");
    assert(tree_size(node->domains) == 2);
    node_add_domain(node, "bbbbb.NL");
    assert(tree_size(node->domains) == 2);
    node_add_domain(node, "BBBBB.nl");
    assert(tree_size(node->domains) == 2);
    node_add_domain(node, "bBbBb.nl");
    assert(tree_size(node->domains) == 2);
}

int
main(int argc, char** argv) {

    test_node_print();
    test_node_shares_element();
    test_node_cache_add_1();
    test_node_cache_add_2();
    test_node_cache_add_3();
    test_node_to_json();
    test_pkt_info_to_json();
    test_node_cache_domains_case();
    return 0;
}
