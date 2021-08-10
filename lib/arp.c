#include <assert.h>
#include <errno.h>

#include "arp.h"
#include "spin_log.h"
#include "statistics.h"

STAT_MODULE(arp)

arp_table_t* arp_table_create(enum arp_table_backend backend) {
    arp_table_t* arp_table = (arp_table_t*) malloc(sizeof(arp_table_t));
    arp_table->backend = backend;
    arp_table->entries = tree_create(cmp_ips);
    return arp_table;
}

void arp_table_destroy(arp_table_t* arp_table) {
    tree_destroy(arp_table->entries);
    free(arp_table);
}

void arp_table_add(arp_table_t* arp_table, ip_t* ip, char* mac) {
    tree_add(arp_table->entries, sizeof(ip_t), ip, strlen(mac) + 1, mac, 1);
}

static void
arp_table_add_ipstr(arp_table_t* arp_table, char* ip_str, char* mac) {
    ip_t ip;
    if (!spin_pton(&ip, ip_str)) {
        spin_log(LOG_ERR, "bad address, ignoring\n");
        return;
    }

    arp_table_add(arp_table, &ip, mac);
}

static void
arp_table_read_linux(arp_table_t* arp_table) {
    FILE *fp;
    char ip[INET6_ADDRSTRLEN];
    char mac[18];
    char ignore[40];
    int result = 4;
    char line[1024];
    STAT_COUNTER(ctr, arp-table-read, STAT_TOTAL);
    spin_log(LOG_DEBUG, "[ARP] Reading ARP table\n");
    fp = popen("ip neigh", "r");
    STAT_VALUE(ctr, fp != NULL);
    if (fp == NULL) {
        spin_log(LOG_ERR, "[ARP] error running ip neigh: %s\n", strerror(errno));
        return;
    }
    /* Read the output a line at a time - output it. */
    while (fgets(line, 1024, fp) != NULL) {
        result = sscanf(line, "%s dev %s lladdr %s %s\n", ip, ignore, mac, ignore);
        if (result == 4) {
            spin_log(LOG_DEBUG, "[ARP] Adding mac %s IP %s to arp cache\n", mac, ip);
            arp_table_add_ipstr(arp_table, ip, mac);
        } else {
            spin_log(LOG_DEBUG, "[ARP] Warning: unrecognized line in arp results (%d)\n", result);
        }
    }

    pclose(fp);
}

void arp_table_read(arp_table_t* arp_table) {
    switch (arp_table->backend) {
    case ARP_TABLE_LINUX:
        arp_table_read_linux(arp_table);
        break;
    case ARP_TABLE_VIRTUAL:
        /* NOOP; nothing to do */
        break;
    default:
        assert(0);
    }
}

void arp_table_print(arp_table_t* arp_table) {
    char ip_str[INET6_ADDRSTRLEN];
    tree_entry_t* cur = tree_first(arp_table->entries);

    spin_log(LOG_DEBUG, "[arp table]\n");
    while (cur != NULL) {
        spin_ntop(ip_str, cur->key, INET6_ADDRSTRLEN);
        spin_log(LOG_DEBUG, "%s %s\n", ip_str, (char*)cur->data);
        cur = tree_next(cur);
    }
    spin_log(LOG_DEBUG, "[end of arp table]\n");
}

int arp_table_size(arp_table_t* arp_table) {
    return tree_size(arp_table->entries);
}

char* arp_table_find_by_ip(arp_table_t* arp_table, ip_t* ip) {
    tree_entry_t* entry = tree_find(arp_table->entries, sizeof(ip_t), ip);
    if (entry != NULL) {
        return (char*)entry->data;
    } else {
        return NULL;
    }
}

char* arp_table_find_by_str(arp_table_t* arp_table, char* ip_str) {
    ip_t ip;
    if (spin_pton(&ip, ip_str)) {
        return arp_table_find_by_ip(arp_table, &ip);
    } else {
        return NULL;
    }
}
