#include <sys/socket.h>
#include <arpa/inet.h>

#include <assert.h>
#include <err.h>
#include <string.h>

#include "ipt.h"

void
ipt_from_uint8t(ip_t *ip_t, char *ipstr, size_t ipstr_len, const uint8_t *ip,
    uint8_t af)
{
	assert(af == AF_INET || af == AF_INET6);
	
	if (inet_ntop(af, ip, ipstr, ipstr_len) == NULL) {
		err(1, "inet_ntop");
	}
	
	if (spin_pton(ip_t, ipstr) == 0) {
		errx(1, "spin_pton");
	}
}
