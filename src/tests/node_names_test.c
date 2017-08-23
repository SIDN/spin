
#include "node_names.h"

#include <assert.h>

void
check_ip(node_names_t* node_names, char* ip_str, char* expected) {
    ip_t ip;
    char* name;

    assert(spin_pton(&ip, ip_str));

    name = node_names_find_ip(node_names, &ip);
    if (expected == NULL) {
        assert(name == 0);
    } else {
        assert(name != NULL);
        assert(strcmp(name, expected) == 0);
    }
}

void
check_mac(node_names_t* node_names, char* mac_str, char* expected) {
    char* name = node_names_find_mac(node_names, mac_str);
    if (expected == NULL) {
        assert(name == 0);
    } else {
        assert(name != NULL);
        assert(strcmp(name, expected) == 0);
    }
}

void
test_read_userconfig() {
    node_names_t* node_names = node_names_create();

    assert(tree_size(node_names->user_names_by_ip) == 0);
    assert(tree_size(node_names->user_names_by_mac) == 0);

    node_names_read_userconfig(node_names, "testdata/node_names_spin_userdata.conf");

    assert(tree_size(node_names->user_names_by_ip) == 6);
    assert(tree_size(node_names->user_names_by_mac) == 2);

    check_ip(node_names, "192.0.2.1", "some_other_host");
    check_ip(node_names, "192.0.2.2", "number two");
    check_ip(node_names, "2001:DB8:beef:1::80", "server");
    check_ip(node_names, "213.152.224.1", "open");
    check_ip(node_names, "2001:DB8:beef:1::123:aaaa", "barserver");
    check_ip(node_names, "192.0.2.123", "one two three");
    check_ip(node_names, "192.0.2.222", NULL);
    check_ip(node_names, "2001:DB8:ff:ff:ff::", NULL);

    check_mac(node_names, "00:02:aa:c1:23:92", "replaces bar");
    check_mac(node_names, "aa:bb:cc:dd:ee:ff", "asdfasdf");
    check_mac(node_names, "ff:ee:dd:cc:bb:aa", NULL);

    node_names_destroy(node_names);
}

void
test_read_dhcpconfig() {
    node_names_t* node_names = node_names_create();

    assert(tree_size(node_names->dhcp_names_by_ip) == 0);
    assert(tree_size(node_names->dhcp_names_by_mac) == 0);

    node_names_read_dhcpconfig(node_names, "testdata/node_names_dhcp.conf");

    check_ip(node_names, "192.0.2.1", "some host");
    check_ip(node_names, "192.0.2.155", "Foo");
    check_ip(node_names, "192.0.2.156", "Foo \\' dus");
    check_ip(node_names, "192.0.2.50", "bar stuff");
    check_ip(node_names, "192.0.2.51", "bar\\\" enzo");
    check_ip(node_names, "192.0.2.100", NULL);

    check_mac(node_names, "18:aa:6a:23:55:dc", "some host");
    check_mac(node_names, "00:11:55:0a:ed:bf", "Foo");
    check_mac(node_names, "00:11:55:0a:ed:bb", "Foo \\' dus");
    check_mac(node_names, "00:02:aa:c1:23:92", "bar stuff");
    check_mac(node_names, "00:02:aa:c1:23:93", "bar\\\" enzo");
    check_mac(node_names, "aa:bb:cc:dd:ee:ff", NULL);

    assert(tree_size(node_names->dhcp_names_by_ip) == 5);
    assert(tree_size(node_names->dhcp_names_by_mac) == 5);

    node_names_destroy(node_names);
}

void
test_read_both() {
    node_names_t* node_names = node_names_create();

    assert(tree_size(node_names->dhcp_names_by_ip) == 0);
    assert(tree_size(node_names->dhcp_names_by_mac) == 0);

    check_ip(node_names, "192.0.2.1", NULL);

    node_names_read_dhcpconfig(node_names, "testdata/node_names_dhcp.conf");

    check_ip(node_names, "192.0.2.1", "some host");

    node_names_read_userconfig(node_names, "testdata/node_names_spin_userdata.conf");

    check_ip(node_names, "192.0.2.1", "some_other_host");

    node_names_destroy(node_names);
}

void
test_aaaa() {
    node_names_t* node_names = node_names_create();

    assert(tree_size(node_names->dhcp_names_by_ip) == 0);
    assert(tree_size(node_names->dhcp_names_by_mac) == 0);

    check_ip(node_names, "192.0.2.1", NULL);

    node_names_read_dhcpconfig(node_names, "testdata/full_dhcp");

    node_names_destroy(node_names);
}

void
test_add_by_ip() {
    node_names_t* node_names = node_names_create();
    ip_t ip;

    assert(tree_size(node_names->user_names_by_ip) == 0);
    assert(tree_size(node_names->user_names_by_mac) == 0);

    check_ip(node_names, "192.0.2.1", NULL);

    spin_pton(&ip, "192.0.2.1");
    node_names_add_user_name_ip(node_names, &ip, "foo");

    assert(tree_size(node_names->user_names_by_ip) == 1);
    assert(tree_size(node_names->user_names_by_mac) == 0);
    check_ip(node_names, "192.0.2.1", "foo");

    node_names_add_user_name_ip(node_names, &ip, "bar");
    assert(tree_size(node_names->user_names_by_ip) == 1);
    assert(tree_size(node_names->user_names_by_mac) == 0);
    check_ip(node_names, "192.0.2.1", "bar");

    node_names_destroy(node_names);
}

void
test_add_by_mac() {
    node_names_t* node_names = node_names_create();

    assert(tree_size(node_names->user_names_by_ip) == 0);
    assert(tree_size(node_names->user_names_by_mac) == 0);

    check_mac(node_names, "aa:aa:aa:aa:aa:aa", NULL);

    node_names_add_user_name_mac(node_names, "aa:aa:aa:aa:aa:aa", "foo");

    assert(tree_size(node_names->user_names_by_ip) == 0);
    assert(tree_size(node_names->user_names_by_mac) == 1);
    check_mac(node_names, "aa:aa:aa:aa:aa:aa", "foo");

    node_names_add_user_name_mac(node_names, "aa:aa:aa:aa:aa:aa", "bar");
    assert(tree_size(node_names->user_names_by_ip) == 0);
    assert(tree_size(node_names->user_names_by_mac) == 1);
    check_mac(node_names, "aa:aa:aa:aa:aa:aa", "bar");

    node_names_destroy(node_names);
}

void
test_write_userconfig() {
    node_names_t* node_names = node_names_create();
    node_names_t* node_names2 = node_names_create();
    ip_t ip;

    const char* TEST_OUTPUT_FILE = "/tmp/node_names_test_tmp";

    // need to start with an empty file, so if it exists, unlink it
    remove(TEST_OUTPUT_FILE);

    // reading the file should fail now
    assert(node_names_read_userconfig(node_names, TEST_OUTPUT_FILE) == -1);

    // read DHCP file, and write the userconfig; this should result
    // in an empty file (as only user-set values are stored)
    node_names_read_dhcpconfig(node_names, "testdata/node_names_dhcp.conf");
    assert(tree_size(node_names->user_names_by_ip) == 0);
    assert(tree_size(node_names->user_names_by_mac) == 0);
    assert(tree_size(node_names->dhcp_names_by_ip) == 5);
    assert(tree_size(node_names->dhcp_names_by_mac) == 5);

    node_names_write_userconfig(node_names, TEST_OUTPUT_FILE);
    node_names_read_userconfig(node_names2, TEST_OUTPUT_FILE);
    assert(tree_size(node_names2->user_names_by_ip) == 0);
    assert(tree_size(node_names2->user_names_by_mac) == 0);
    assert(tree_size(node_names2->dhcp_names_by_ip) == 0);
    assert(tree_size(node_names2->dhcp_names_by_mac) == 0);

    // add some values, then write and read again
    spin_pton(&ip, "192.0.2.1");
    node_names_add_user_name_ip(node_names, &ip, "foo");
    spin_pton(&ip, "192.0.2.2");
    node_names_add_user_name_ip(node_names, &ip, "bar");
    node_names_add_user_name_mac(node_names, "aa:aa:aa:aa:aa:aa", "foo");
    node_names_add_user_name_mac(node_names, "bb:bb:bb:bb:bb:bb", "bar");
    node_names_write_userconfig(node_names, TEST_OUTPUT_FILE);
    node_names_read_userconfig(node_names2, TEST_OUTPUT_FILE);
    assert(tree_size(node_names2->user_names_by_ip) == 2);
    assert(tree_size(node_names2->user_names_by_mac) == 2);
    assert(tree_size(node_names2->dhcp_names_by_ip) == 0);
    assert(tree_size(node_names2->dhcp_names_by_mac) == 0);

    node_names_destroy(node_names);
    node_names_destroy(node_names2);
}


int main(int argc, char** argv) {
    test_read_userconfig();
    test_read_dhcpconfig();
    test_read_both();
    test_aaaa();
    test_add_by_ip();
    test_add_by_mac();
    test_write_userconfig();
    return 0;
}

