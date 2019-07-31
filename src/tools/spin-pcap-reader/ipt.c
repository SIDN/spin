#include <assert.h>
#include <string.h>

#include "ipt.h"

void
ipt_from_uint8t(ip_t *ip_t, const uint8_t *ip, uint8_t family)
{
	assert(family == AF_INET || family == AF_INET6);

	ip_t->family = family;
	if (family == AF_INET) {
		memset(ip_t->addr, 0, 12);
		memcpy(ip_t->addr + 12, ip, 4); // XXX 4
	} else { /* AF_INET6 */
		memcpy(ip_t->addr, ip, 16); // XXX 16
	}
}
