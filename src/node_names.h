/**
 * This module looks up names for nodes
 *
 * It will check if a user has set a name (stored in /etc/spin/userdata.c)
 * If not user-set, the mac address will be looked up in the DHCP config
 * (/etc/config/dhcp)
 *
 * Note: these are only read *once* (in a future update, maybe we can re-read if the file changes?
 */

#include "tree.h"
#include "util.h"

// hmm. just make two trees per input for easy reference?
typedef struct {
    // referenced by IP
    tree_t* user_names_by_ip;
    tree_t* user_names_by_mac;
    // referenced by MAC
    tree_t* dhcp_names_by_ip;
    tree_t* dhcp_names_by_mac;
} node_names_t;

node_names_t* node_names_create(void);
void node_names_destroy(node_names_t*);

int node_names_read_dhcpconfig(node_names_t* node_names, const char* filename);
int node_names_read_userconfig(node_names_t* node_names, const char* filename);
int node_names_write_userconfig(node_names_t* node_names, const char* filename);

char* node_names_find_ip(node_names_t* node_names, uint8_t* ip);
char* node_names_find_mac(node_names_t* node_names, char* mac);

void node_names_add_user_name_ip(node_names_t* node_names, uint8_t* ip, char* name);
void node_names_add_user_name_mac(node_names_t* node_names, char* mac, char* name);
