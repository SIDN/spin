#include <stddef.h>

struct ether_addr;

int macstr_from_ea(struct ether_addr *ea, char *s, size_t len);
void macstr_from_uint8t(const uint8_t *mac, char *s, size_t len);
