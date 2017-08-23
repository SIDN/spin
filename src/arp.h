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

typedef struct {
    tree_t* entries;
} arp_table_t;

arp_table_t* arp_table_create(void);
void arp_table_destroy(arp_table_t* arp_table);

void arp_table_read(arp_table_t* arp_table);
void arp_table_print(arp_table_t* arp_table);

void arp_table_add(arp_table_t* arp_table, char* ip, char* mac);
int arp_table_size(arp_table_t* arp_table);

char* arp_table_find_by_ip(arp_table_t* arp_table, ip_t* ip);
char* arp_table_find_by_str(arp_table_t* arp_table, char* ip_str);

#endif // SPIN_ARP_H
