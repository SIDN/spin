#include "config.h"

#include <sys/types.h>
#include <sys/socket.h>

#include <net/if.h>
#include <netinet/in.h>
#ifdef HAVE_NET_ETHERNET_H
#include <net/ethernet.h>
#endif
#ifdef HAVE_NETINET_IF_ETHER_H
#include <netinet/if_ether.h>
#endif

#include <stdio.h>
#include <string.h>

#include "macstr.h"

int
macstr_from_ea(struct ether_addr *ea, char *s, size_t len)
{
	return snprintf(s, len, "%02x:%02x:%02x:%02x:%02x:%02x",
	    ea->ether_addr_octet[0], ea->ether_addr_octet[1],
	    ea->ether_addr_octet[2], ea->ether_addr_octet[3],
	    ea->ether_addr_octet[4], ea->ether_addr_octet[5]);
}

void
macstr_from_uint8t(const uint8_t *mac, char *s, size_t len)
{
	struct ether_addr ea;

	memcpy(&ea.ether_addr_octet, mac, sizeof(ea.ether_addr_octet));
	macstr_from_ea(&ea, s, len);
}
