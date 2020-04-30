// should we cache this? do we need to update it on every lookup anyway?
// hmm, maybe only when it's not found anyway

#ifndef SPIN_ARP_H
#define SPIN_ARP_H 1

#include "tree.h"
#include "util.h"

#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>

/*
 * arp_table_t contains an ARP table. Depending on the backend type that is
 * specified upon creation with arp_table_create(), some functions have
 * different behavior. See below for details.
 */

enum arp_table_backend {
    ARP_TABLE_LINUX,
    ARP_TABLE_VIRTUAL,
};

typedef struct {
    enum arp_table_backend backend;
    tree_t* entries;
} arp_table_t;

arp_table_t* arp_table_create(enum arp_table_backend backend);
void arp_table_destroy(arp_table_t* arp_table);

/*
 * arp_table_add() can be used to add an entry to the specified ARP table.
 * It should only be necessary to use this function when the backend type is
 * ARP_TABLE_VIRTUAL. When the backend type is ARP_TABLE_LINUX, various calls
 * to the arp_table_read() function spread throughout the SPIN code make sure
 * the ARP table is populated and up-to-date.
 */
void arp_table_add(arp_table_t* arp_table, ip_t* ip, char* mac);

/*
 * When the backend type of this ARP table is ARP_TABLE_LINUX, this function
 * queries the Linux neighbour table (ARP/NDISC) and updates the specified
 * ARP table. When the backend type is ARP_TABLE_VIRTUAL, this function is
 * a NOOP.
 */
void arp_table_read(arp_table_t* arp_table);

char* arp_table_find_by_ip(arp_table_t* arp_table, ip_t* ip);

#endif // SPIN_ARP_H
