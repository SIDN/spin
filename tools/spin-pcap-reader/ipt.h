#include "util.h"

/*
 * Convert IP address to ip_t type. Also provides a string representation
 * in the provided string.
 */
void ipt_from_uint8t(ip_t *, char*, size_t, const uint8_t *, uint8_t);
