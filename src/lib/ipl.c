#include "pkt_info.h"
#include "util.h"
#include "ipl.h"
#include "spin_list.h"
#include "node_cache.h"
#include "dns_cache.h"
#include "tree.h"
#include "spin_log.h"

static struct list_info* ipl_list_ar;

#define ipl_block ipl_list_ar[IPLIST_BLOCK]
#define ipl_ignore ipl_list_ar[IPLIST_IGNORE]
#define ipl_allow ipl_list_ar[IPLIST_ALLOW]

void init_ipl(struct list_info *lip) {
    int cnt;

    lip->li_tree = tree_create(cmp_ips);
    cnt = read_ip_tree(lip->li_tree, lip->li_filename);
    spin_log(LOG_DEBUG, "File %s, read %d entries\n", lip->li_filename, cnt);
}

void init_all_ipl(struct list_info *ipl_list_ar_a) {
    int i;
    struct list_info *lip;

    ipl_list_ar = ipl_list_ar_a;

    for (i=0; i<N_IPLIST; i++) {
        lip = &ipl_list_ar[i];
        init_ipl(lip);
    }
}

void
add_ip_tree_to_li(tree_t* tree, struct list_info *lip) {
    tree_entry_t* cur;

    if (tree == NULL)
        return;
    cur = tree_first(tree);
    while(cur != NULL) {
        tree_add(lip->li_tree, cur->key_size, cur->key, cur->data_size, cur->data, 1);
        cur = tree_next(cur);
    }
    lip->li_modified++;
}

void
remove_ip_tree_from_li(tree_t *tree, struct list_info *lip) {
    tree_entry_t* cur;

    if (tree == NULL) {
        return;
    }
    cur = tree_first(tree);
    while(cur != NULL) {
        tree_remove(lip->li_tree, cur->key_size, cur->key);
        cur = tree_next(cur);
    }
    lip->li_modified++;
}

void remove_ip_from_li(ip_t* ip, struct list_info *lip) {

    tree_remove(lip->li_tree, sizeof(ip_t), ip);
    lip->li_modified++;
}

int ip_in_li(ip_t* ip, struct list_info* lip) {

    return tree_find(lip->li_tree, sizeof(ip_t), ip) != NULL;
}

int ip_in_ignore_list(ip_t* ip) {

    return ip_in_li(ip, &ipl_list_ar[IPLIST_IGNORE]);
}

// hmz, we probably want to refactor ip_t/the tree list
// into something that requires less data copying
int addr_in_ignore_list(int family, uint8_t* addr) {
    ip_t ip;

    ip.family = family;
    memcpy(ip.addr, addr, 16);
    return ip_in_ignore_list(&ip);
}
