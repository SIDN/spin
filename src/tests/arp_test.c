
#include "arp.h"

#include <assert.h>

void
test_read() {
    arp_table_t* arp_table = arp_table_create();
    arp_table_read(arp_table);
    arp_table_print(arp_table);
    arp_table_destroy(arp_table);
}

void
test_add() {
    arp_table_t* arp_table = arp_table_create();

    assert(arp_table_size(arp_table) == 0);

    arp_table_add(arp_table, "127.0.0.1", "aa:bb:cc:dd:ee:ff");
    arp_table_add(arp_table, "::1", "aa:bb:cc:dd:ee:ff");

    assert(arp_table_size(arp_table) == 2);

    arp_table_destroy(arp_table);
}

void
test_find() {
    arp_table_t* arp_table = arp_table_create();
    char* mac;

    assert(arp_table_size(arp_table) == 0);

    arp_table_add(arp_table, "127.0.0.1", "aa:bb:cc:dd:ee:ff");
    arp_table_add(arp_table, "::1", "ff:ee:dd:cc:bb:aa");
    arp_table_add(arp_table, "bad_address", "bb:bb:bb:bb:bb:bb");

    assert(arp_table_size(arp_table) == 2);

    mac = arp_table_find_by_str(arp_table, "127.0.0.2");
    assert(mac == NULL);

    mac = arp_table_find_by_str(arp_table, "127.0.0.1");
    assert(strcmp(mac, "aa:bb:cc:dd:ee:ff") == 0);

    mac = arp_table_find_by_str(arp_table, "::1");
    assert(strcmp(mac, "ff:ee:dd:cc:bb:aa") == 0);

    mac = arp_table_find_by_str(arp_table, "bad_address");
    assert(mac == NULL);

    arp_table_destroy(arp_table);
}

int
main(int argc, char** argv) {
    test_read();
    test_add();
    test_find();
    return 0;
}
