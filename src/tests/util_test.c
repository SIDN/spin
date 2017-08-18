
#include "util.h"

#include <assert.h>
#include <unistd.h>

void check_bufstr(buffer_t* buffer, const char* expected) {
    int cmp;

    if (!buffer->finished) {
        printf("Error, buffer not finished\n");
        assert(0);
    }
    cmp = strcmp(buffer_str(buffer), expected);
    if (cmp != 0) {
        printf("Error, buffer string: '%s', expected '%s'\n", buffer_str(buffer), expected);
        assert(0);
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

    printf("[xx] write: %d\n", buffer_write(buf, "12345"));
    printf("[xx] write: %d\n", buffer_write(buf, "67890"));

    buffer_finish(buf);
    printf("[XX] %s\n", buffer_str(buf));

    buffer_destroy(buf);
}

// note; if this test fails it may leave a tmp file around
void
test_ip_tree_read_write() {
    tree_t* tree = tree_create(cmp_ips);
    uint8_t ip[17];
    char tmpfile[] = "/tmp/spin_util_test_XXXXXX";

    // get a file to use
    int fd = mkstemp(tmpfile);
    assert(fd >= 0);
    close(fd);
    unlink(tmpfile);

    // reading nonexistent file should return -1
    assert(read_ip_tree(tree, tmpfile) < 0);

    // add a few values and write it out
    spin_pton(ip, "192.0.2.1");
    tree_add(tree, 17, ip, 0, NULL, 1);
    spin_pton(ip, "192.0.2.2");
    tree_add(tree, 17, ip, 0, NULL, 1);
    spin_pton(ip, "192.0.2.3");
    tree_add(tree, 17, ip, 0, NULL, 1);
    spin_pton(ip, "::1");
    tree_add(tree, 17, ip, 0, NULL, 1);
    spin_pton(ip, "2001:DB8::1:1");
    tree_add(tree, 17, ip, 0, NULL, 1);
    spin_pton(ip, "2001:DB8::1:2");
    tree_add(tree, 17, ip, 0, NULL, 1);

    assert(store_ip_tree(tree, tmpfile));

    // see if we can read them now
    tree_clear(tree);
    assert(tree_size(tree) == 0);

    assert(read_ip_tree(tree, tmpfile));
    assert(tree_size(tree) == 6);

}

int main(int argc, char** argv) {
    /*
    test_buffer_write_1();
    test_buffer_write_2();
    */
    test_ip_tree_read_write();
    return 0;
}
