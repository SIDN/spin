/**
 * Code to read /proc/net/arp
 */

// should we cache this? do we need to update it on every lookup anyway?
// hmm, maybe only when it's not found anyway

#ifndef SPIN_ARP_H
#define SPIN_ARP_H 1

#include "tree.h"
#include "util.h"

#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>

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

void arp_table_add_ip_t(arp_table_t* arp_table, ip_t* ip, char* mac);
void arp_table_add(arp_table_t* arp_table, char* ip_str, char* mac);

void arp_table_read(arp_table_t* arp_table);

char* arp_table_find_by_ip(arp_table_t* arp_table, ip_t* ip);

#endif // SPIN_ARP_H
