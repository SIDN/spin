#include "spin_list.h"
#include "ipl.h"
#include "node_cache.h"

/*
 * The three lists of IP addresses are now kept in memory in spind, with
 * a copy written to file.
 * BLOCK, IGNORE, ALLOW
 */
struct list_info ipl_list_ar[N_IPLIST] = {
    { 0, "block",   0 },
    { 0, "ignore",  0 },
    { 0, "allow",   0 },
};

struct list_info* get_spin_iplists() {
    return ipl_list_ar;
};

struct list_info* get_spin_iplist(int index) {
    return &ipl_list_ar[index];
};

// Returns -1 if list not found
int get_spin_iplist_id_by_name(const char* name) {
    for (int i=0; i < N_IPLIST; i++) {
        if (strncmp(name, ipl_list_ar[i].li_listname, strlen(name)) == 0) {
            return i;
        }
    }
    return -1;
};


// TODO: REMOVE
void add_ip_to_list(node_cache_t* node_cache, int list_id, ip_t* ip) {
    add_ip_to_li(ip, &ipl_list_ar[list_id]);
}

void remove_ip_from_list(node_cache_t* node_cache, int list_id, ip_t* ip) {
    remove_ip_from_li(ip, &ipl_list_ar[list_id]);
}

void add_node_to_list(node_cache_t* node_cache, int list_id, node_t* node) {
    
}
