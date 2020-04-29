#include <sys/socket.h>

#include <assert.h>
#include <err.h>

/* spind/lib includes */
#include "extsrc.h"
#include "socket.h"
#include "util.h"

#include "arpupdate.h"
#include "ipt.h"
#include "macstr.h"

/* #define ARPUPDATE_DEBUG */

static void
send_arp_table_update_to_spind(int fd, struct extsrc_arp_table_update *up)
{
	struct extsrc_msg *msg;

	msg = extsrc_msg_create_arp_table_update(up);

	socket_writemsg(fd, msg->data, msg->length);

	extsrc_msg_free(msg);
}

void
arp_update(int fd, const uint8_t *mac, const uint8_t *ip, uint8_t family)
{
	struct extsrc_arp_table_update up;
	char ipstr[INET6_ADDRSTRLEN];

	assert(family == AF_INET || family == AF_INET6);

	ipt_from_uint8t(&up.ip, ipstr, sizeof(ipstr), ip, family);
	macstr_from_uint8t(mac, up.mac, sizeof(up.mac));

	send_arp_table_update_to_spind(fd, &up);

#ifdef ARPUPDATE_DEBUG
	warnx("MAC: %s, IP: %s", up.mac, ipstr);
#endif
}
