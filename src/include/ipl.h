#ifndef SPIN_IPL_H
#define SPIN_IPL_H 1

/*
 * Lowlevel functionality to manipulate lists of IP addresses
 */
#include "spin_list.h"

struct list_info {
    tree_t *        li_tree;                 // Tree of IP addresses
    char *          li_listname;             // Name of list
    int             li_modified;             // File should be written
};

char* ipl_filename(struct list_info *lip);
void init_ipl(struct list_info *lip);
void init_all_ipl(struct list_info *ipl_list_ar);
void clean_all_ipl();
void add_ip_to_li(ip_t* ip, struct list_info *lip);
void add_ip_tree_to_li(tree_t* tree, struct list_info *lip);
void remove_ip_tree_from_li(tree_t *tree, struct list_info *lip);
void remove_ip_from_li(ip_t* ip, struct list_info *lip);
int ip_in_li(ip_t* ip, struct list_info* lip);
int ip_in_ignore_list(ip_t* ip);
int addr_in_ignore_list(int family, uint8_t* addr);

#endif // SPIN_IPL_H
