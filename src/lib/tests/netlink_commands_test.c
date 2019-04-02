
#include "util.h"
#include "netlink_commands.h"

#include "test_helper.h"

#define ASSERT(stmt) assert((
void
test_add_ip() {
    uint8_t ip[16];
    netlink_command_result_t* cr = netlink_command_result_create();
    int i;

    inet_pton(AF_INET, "127.0.0.1", ip);
    for (i = 0; i < 100; i++) {
        netlink_command_result_add_ip(cr, AF_INET, ip);
    }
    inet_pton(AF_INET6, "::1", ip);
    for (i = 0; i < 100; i++) {
        netlink_command_result_add_ip(cr, AF_INET6, ip);
    }

    assertf(cr->ip_count == 200, "%lu", cr->ip_count);
    assertf(cr->ip_max == 320, "ip_max: %lu", cr->ip_max);

    netlink_command_result_destroy(cr);
}

int main(int argc, char** argv) {
    test_add_ip();
    return 0;
}
