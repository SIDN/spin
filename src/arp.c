
#include "arp.h"

arp_table_t* arp_table_create(void) {
    arp_table_t* arp_table = (arp_table_t*) malloc(sizeof(arp_table_t));
    arp_table->entries = tree_create(cmp_ips);
    return arp_table;
}

void arp_table_destroy(arp_table_t* arp_table) {
    tree_destroy(arp_table->entries);
    free(arp_table);
}

void arp_table_read(arp_table_t* arp_table) {
    FILE *fp;
    char ip[INET6_ADDRSTRLEN];
    char mac[18];
    char ignore[40];
    int result = 4;

    fp = popen("ip neigh", "r");
    if (fp == NULL) {
        printf("error running ip neigh\n");
        return;
    }
    /* Read the output a line at a time - output it. */

    /*while (fgets(line, sizeof(line)-1, fp) != NULL) {
        printf("%s", line);
    }*/

    while(result == 4) {
        result = fscanf(fp, "%s dev %s lladdr %s %s\n", ip, ignore, mac, ignore);
        if (result == 4) {
            arp_table_add(arp_table, ip, mac);
        }
    }

    /* close */
    pclose(fp);
}

void arp_table_print(arp_table_t* arp_table) {
    char ip_str[INET6_ADDRSTRLEN];
    tree_entry_t* cur = tree_first(arp_table->entries);

    printf("[arp table]\n");
    while (cur != NULL) {
        spin_ntop(ip_str, cur->key, INET6_ADDRSTRLEN);
        printf("%s %s\n", ip_str, (char*)cur->data);
        cur = tree_next(cur);
    }
    printf("[end of arp table]\n");
}

void arp_table_add(arp_table_t* arp_table, char* ip, char* mac) {
    // convert ip to a 17-byte array
    uint8_t ip_bytes[17];

    if (!spin_pton(ip_bytes, ip)) {
        //printf("[XX] error, bad address, ignoring\n");
        return;
    }

    tree_add(arp_table->entries, 17, ip_bytes, strlen(mac) + 1, mac, 1);
}

int arp_table_size(arp_table_t* arp_table) {
    return tree_size(arp_table->entries);
}

char* arp_table_find_by_ip(arp_table_t* arp_table, uint8_t* ip) {
    tree_entry_t* entry = tree_find(arp_table->entries, 17, ip);
    if (entry != NULL) {
        return (char*)entry->data;
    } else {
        return NULL;
    }
}

char* arp_table_find_by_str(arp_table_t* arp_table, char* ip_str) {
    uint8_t ip[17];
    if (spin_pton(ip, ip_str)) {
        return arp_table_find_by_ip(arp_table, ip);
    } else {
        return NULL;
    }
}

