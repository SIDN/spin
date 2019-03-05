#include <string.h>
#include <errno.h>

#include "arp.h"
#include "spin_log.h"
#include "statistics.h"

STAT_MODULE(arp)

arp_table_t* arp_table_create(void) {
    arp_table_t* arp_table = (arp_table_t*) malloc(sizeof(arp_table_t));
    arp_table->entries = tree_create(cmp_ips);
    return arp_table;
}

void arp_table_destroy(arp_table_t* arp_table) {
    tree_destroy(arp_table->entries);
    free(arp_table);
}

static
void arp_table_add(arp_table_t* arp_table, char* ip_str, char* mac) {
    ip_t ip;

    if (!spin_pton(&ip, ip_str)) {
        //spin_log(LOG_ERR, "[XX] error, bad address, ignoring\n");
        return;
    }

    tree_add(arp_table->entries, sizeof(ip_t), &ip, strlen(mac) + 1, mac, 1);
}

void arp_table_read(arp_table_t* arp_table) {
    FILE *fp;
    char ip[INET6_ADDRSTRLEN];
    char mac[18];
    char ignore[40];
    int result = 4;
    char line[1024];

    fp = popen("ip neigh", "r");
    if (fp == NULL) {
        spin_log(LOG_ERR, "error running ip neigh: %s\n", strerror(errno));
        return;
    }
    /* Read the output a line at a time - output it. */
    while (fgets(line, 1024, fp) != NULL) {
        result = sscanf(line, "%s dev %s lladdr %s %s\n", ip, ignore, mac, ignore);
        if (result == 4) {
            arp_table_add(arp_table, ip, mac);
        }
    }

    pclose(fp);
}

#ifdef notdef
static
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
#endif

#ifdef notdef
static
int arp_table_size(arp_table_t* arp_table) {
    return tree_size(arp_table->entries);
}
#endif

char* arp_table_find_by_ip(arp_table_t* arp_table, ip_t* ip) {
    tree_entry_t* entry = tree_find(arp_table->entries, sizeof(ip_t), ip);
    if (entry != NULL) {
        return (char*)entry->data;
    } else {
        return NULL;
    }
}

#ifdef notdef
static
char* arp_table_find_by_str(arp_table_t* arp_table, char* ip_str) {
    ip_t ip;
    if (spin_pton(&ip, ip_str)) {
        return arp_table_find_by_ip(arp_table, &ip);
    } else {
        return NULL;
    }
}
#endif
