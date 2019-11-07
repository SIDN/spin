#ifndef SPIN_LIST_H
#define SPIN_LIST_H

/*
 * SPIN internally maintains 3 lists of IP addresses, which influence
 * its behaviour:
 * - ignore: traffic containing one of these ips is completely ignored;
 *           it is not shown in the traffic stream and cannot otherwise
 *           be interacted with
 * - block: traffic from and to these ips is blocked with a firewall
 *          rule. The rule is set to log messages, so this blocked
 *          traffic shows up in the traffic stream as blocked
 * - allow: traffic from and to these ips is allowed, even if the
 *          other ip address is blocked. For example, if you block
 *          all traffic from and to 192.0.2.1 for all your devices,
 *          but want one specific device to be able to contact it,
 *          you add the device's ips to 'allow'.
 *
 * Low-level functionality, such as manipulation of lists themselves,
 * is in ipl.[ch]. Please not that modifying these lists may require
 * some calls to the node_cache as well, since it has some information
 * about these lists for efficiency.
 */

#include "util.h"

enum iplist_index {
    IPLIST_BLOCK,
    IPLIST_IGNORE,
    IPLIST_ALLOW,
    N_IPLIST
};

enum spinfunc_command {
    SF_ADD,
    SF_REM,
    N_SF
};

typedef void (*spinfunc)(void*, int listid, int addrem, ip_t *ip_addr);

void spin_register(char *name, spinfunc wf, void *arg, int list[N_IPLIST]);

struct list_info* get_spin_iplists();
struct list_info* get_spin_iplist(int index);
/*
 * Returns -1 if list not found
 */
int get_spin_iplist_id_by_name(const char* name);

#endif
