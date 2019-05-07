
#include "util.h"

#include "test_helper.h"
#include "pkt_info.h"

#include <unistd.h>


void cmp_int_helper(int a, int b, int expected) {
    int result = cmp_ints(sizeof(a), &a, sizeof(b), &b);
    assertf(result == expected, "comparison of %d and %d returned %d, expected %d", a, b, result, expected);
}

void test_cmp_ints() {
    cmp_int_helper(0, 0, 0);
    cmp_int_helper(1, 0, 1);
    cmp_int_helper(0, 1, -1);
    cmp_int_helper(12345, -12345, 1);
    cmp_int_helper(-12345, 12345, -1);
}

void cmp_str_helper(char* a, char* b, int expected) {
    int result = cmp_strs(strlen(a), a, strlen(b), b);
    assertf(result == expected, "comparison of %s and %s returned %d, expected %d", a, b, result, expected);
}

void test_cmp_strs() {
    cmp_str_helper("aaaa", "aaaa", 0);
    cmp_str_helper("bbbb", "aaaa", 1);
    cmp_str_helper("aaaa", "bbbb", -1);
    cmp_str_helper("", "", 0);
    cmp_str_helper("", "aaaa", -1);
    cmp_str_helper("aaaa", "", 1);
    cmp_str_helper("aaaa", "aaa", 1);
    cmp_str_helper("aaaa", "aaaaa", -1);
}

void cmp_dname_helper(uint8_t* a, uint8_t* b, int expected) {
    int result = cmp_domains(sizeof(a), a, sizeof(b), b);
    char a_str[256], b_str[256];
    dns_dname2str(a_str, (char*)a, 256);
    dns_dname2str(b_str, (char*)b, 256);
    assertf(result == expected, "comparison of %s and %s returned %d, expected %d", a_str, b_str, result, expected);
}

void test_cmp_dnames() {
    // note: cmp_dname does not compare according to canonical order,
    // it is merely a way to sort them in a consistent manner internally
    uint8_t dname1[] = { 0x03, 0x77, 0x77, 0x77, 0x04, 0x74, 0x65,
                         0x73, 0x74, 0x02, 0x6e, 0x6c, 0x00 }; // www.test.nl
    uint8_t dname2[] = { 0x03, 0x77, 0x77, 0x77, 0x04, 0x74, 0x61,
                         0x73, 0x74, 0x02, 0x6e, 0x6c, 0x00 }; // www.tast.nl
    uint8_t dname3[] = { 0x04, 0x74, 0x65, 0x73, 0x74, 0x02, 0x6e, 0x6c, 0x00 }; // test.nl
    uint8_t dname4[] = { 0x04, 0x74, 0x61, 0x73, 0x74, 0x02, 0x6e, 0x6c, 0x00 }; // tast.nl
    uint8_t dname5[] = { 0x00 }; // tast.nl

    cmp_dname_helper(dname1, dname1, 0);
    cmp_dname_helper(dname1, dname2, 1);
    cmp_dname_helper(dname2, dname1, -1);
    cmp_dname_helper(dname1, dname3, -1);
    cmp_dname_helper(dname3, dname1, 1);
    cmp_dname_helper(dname1, dname4, -1);
    cmp_dname_helper(dname3, dname4, 1);
    cmp_dname_helper(dname4, dname3, -1);
    cmp_dname_helper(dname5, dname1, -1);
    cmp_dname_helper(dname5, dname2, -1);
    cmp_dname_helper(dname5, dname3, -1);
    cmp_dname_helper(dname5, dname4, -1);
    cmp_dname_helper(dname5, dname5, 0);
}

void ip_pton_ntop_helper(const char* ip_str, int expected_family) {
    ip_t ip;
    char ip_str_out[INET6_ADDRSTRLEN];

    spin_pton(&ip, ip_str);
    assertf(ip.family == expected_family, "wrong family (%d, expected %d)", ip.family, expected_family);
    spin_ntop(ip_str_out, &ip, INET6_ADDRSTRLEN);
    assertf(strcmp(ip_str, ip_str_out) == 0, "IP conversion bad, got %s, expected %s\n", ip_str_out, ip_str);
}

void test_ip_pton_ntop() {
    ip_pton_ntop_helper("192.0.2.1", AF_INET);
    ip_pton_ntop_helper("192.0.2.2", AF_INET);
    ip_pton_ntop_helper("::1", AF_INET6);
    ip_pton_ntop_helper("fff::1", AF_INET6);
}


void check_bufstr(buffer_t* buffer, const char* expected) {
    int cmp;

    if (!buffer->finished) {
        assertf(0, "Error, buffer not finished");
    }
    cmp = strcmp(buffer_str(buffer), expected);
    if (cmp != 0) {
        assertf(0, "Error, buffer string: '%s', expected '%s'\n", buffer_str(buffer), expected);
    }
}

void
test_buffer_write_1() {
    buffer_t* buf = buffer_create(10);

    buffer_write(buf, "foo");
    buffer_write(buf, "bar");
    buffer_finish(buf);
    check_bufstr(buf, "foobar");
    buffer_reset(buf);


    buffer_write(buf, "foo");
    buffer_finish(buf);
    check_bufstr(buf, "foo");

    assert(buffer_write(buf, "bar") == -1);
    check_bufstr(buf, "foo");

    buffer_reset(buf);
    buffer_write(buf, "%s %d", "asdf", 123);
    buffer_finish(buf);
    check_bufstr(buf, "asdf 123");

    buffer_destroy(buf);
}

void
test_buffer_write_2() {
    buffer_t* buf = buffer_create(8);

    buffer_write(buf, "12345");

    assert(buffer_ok(buf));

    buffer_write(buf, "67890");
    buffer_finish(buf);
    assert(!buffer_ok(buf));

    buffer_destroy(buf);
}

void
test_buffer_resize() {
    buffer_t* buf = buffer_create(8);
    buffer_allow_resize(buf);

    buffer_write(buf, "12345");
    buffer_write(buf, "67890");

    assert(buf->max == 16);
    assert(buffer_ok(buf));
    buffer_destroy(buf);
}

void
test_buffer_va_list() {
    const char *fmt = "%s, %s";
    const char *a = "123456";
    const char *b = "abcdef";
    char check[16];

    buffer_t* buf = buffer_create(8);
    buffer_allow_resize(buf);

    buffer_write(buf, fmt, a, b);

    assert(buf->max == 16);
    assert(buffer_ok(buf));
    buffer_finish(buf);

    sprintf(check, fmt, a, b);
    check_bufstr(buf, check);

    buffer_destroy(buf);
}

// note; if this test fails it may leave a tmp file around
void
test_ip_tree_read_write() {
    tree_t* tree = tree_create(cmp_ips);
    ip_t ip;
    char tmpfile[] = "/tmp/spin_util_test_XXXXXX";

    // get a file to use
    int fd = mkstemp(tmpfile);
    assert(fd >= 0);
    close(fd);
    unlink(tmpfile);

    // reading nonexistent file should return -1
    assert(read_ip_tree(tree, tmpfile) < 0);

    // add a few values and write it out
    spin_pton(&ip, "192.0.2.1");
    tree_add(tree, sizeof(ip_t), &ip, 0, NULL, 1);
    spin_pton(&ip, "192.0.2.2");
    tree_add(tree, sizeof(ip_t), &ip, 0, NULL, 1);
    spin_pton(&ip, "192.0.2.3");
    tree_add(tree, sizeof(ip_t), &ip, 0, NULL, 1);
    spin_pton(&ip, "::1");
    tree_add(tree, sizeof(ip_t), &ip, 0, NULL, 1);
    spin_pton(&ip, "2001:DB8::1:1");
    tree_add(tree, sizeof(ip_t), &ip, 0, NULL, 1);
    spin_pton(&ip, "2001:DB8::1:2");
    tree_add(tree, sizeof(ip_t), &ip, 0, NULL, 1);

    assert(store_ip_tree(tree, tmpfile));

    // see if we can read them now
    tree_clear(tree);
    assert(tree_size(tree) == 0);

    assert(read_ip_tree(tree, tmpfile));
    assert(tree_size(tree) == 6);

    tree_destroy(tree);
}

int main(int argc, char** argv) {
    test_cmp_ints();
    test_cmp_strs();
    test_cmp_dnames();
    test_ip_pton_ntop();
    test_buffer_write_1();
    test_buffer_write_2();
    test_buffer_resize();
    test_buffer_va_list();
    test_ip_tree_read_write();
    return 0;
}
