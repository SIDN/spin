#include "config.h"

#ifdef HAVE_NET_ETHERNET_H
#include <net/ethernet.h>
#endif
#ifdef HAVE_NETINET_IF_ETHER_H
#include <netinet/if_ether.h>
#endif

#include <assert.h>
#include <err.h>
#include <socket.h>
#include <time.h>

/* spind/lib includes */
#include "extsrc.h"
#include "node_cache.h"
#include "util.h"

#include "ipt.h"
#include "macstr.h"
#include "nodes.h"

#define ETHER_ADDR_STRLEN sizeof("aa:bb:cc:dd:ee:ff")

/* #define NODES_DEBUG */

static void
send_arp_table_update_to_spind(int fd, struct extsrc_arp_table_update *up)
{
	struct extsrc_msg *msg;

	msg = extsrc_msg_create_arp_table_update(up);

	socket_writemsg(fd, msg->data, msg->length);

	extsrc_msg_free(msg);
}


static node_t *
find_node_by_mac(node_cache_t *node_cache, const uint8_t *mac)
{
	node_t *node;
	struct ether_addr ea;
	char macstr[ETHER_ADDR_STRLEN];
	time_t now;

	now = time(NULL);

	memcpy(&ea.ether_addr_octet, mac, sizeof(ea.ether_addr_octet));
	macstr_from_ea(&ea, macstr, sizeof(macstr));
	node = node_cache_find_by_mac(node_cache, macstr);

	return node;
}

void
mark_local_device(int fd, node_cache_t *node_cache, const uint8_t *mac,
    const uint8_t *ip, uint8_t family)
{
	node_t *node;
	struct extsrc_arp_table_update up;
	time_t now;
#ifdef NODES_DEBUG
	char macstr[ETHER_ADDR_STRLEN];
	char ipstr[INET6_ADDRSTRLEN];
#endif

	assert(family == AF_INET || family == AF_INET6);

	now = time(NULL);

	ipt_from_uint8t(&up.ip, ip, family);

#ifdef NODES_DEBUG
	macstr_from_uint8t(mac, macstr, sizeof(macstr));
	spin_ntop(ipstr, &up.ip, 128);
	warnx("MAC: %s, IP: %s", macstr, ipstr);
#endif

	node = find_node_by_mac(node_cache, mac);
	if (node) {
		if (!tree_find(node->ips, sizeof(ip_t), &up.ip)) {
			node_add_ip(node, &up.ip);

			node_set_modified(node, now);

			macstr_from_uint8t(mac, up.mac, sizeof(up.mac));

			send_arp_table_update_to_spind(fd, &up);
#ifdef NODES_DEBUG
			warnx("send_arp_table_update_to_spind");
#endif
		}
	}
}

void
x_node_cache_add_mac_uint8t(node_cache_t *node_cache, const uint8_t *mac)
{
	char macstr[ETHER_ADDR_STRLEN];

	macstr_from_uint8t(mac, macstr, sizeof(macstr));
	x_node_cache_add_mac_macstr(node_cache, macstr);
}

void
x_node_cache_add_mac_macstr(node_cache_t *node_cache, char *mac)
{
	time_t now;

	now = time(NULL);

	node_t *node;
	node = node_create(0);
	if (!node) {
		err(1, "node_create");
	}

	node_set_mac(node, mac);
	node_set_modified(node, now);
	node_cache_add_node(node_cache, node);
}
