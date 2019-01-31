#ifndef HANDLE_H
#define HANDLE_H 1

#define IPLIST_BLOCK		0
#define IPLIST_IGNORE		1
#define IPLIST_ALLOW		2

void handle_command_get_iplist(int iplist, const char* json_command);
// void handle_command_get_list(config_command_t cmd, const char* json_command);
void handle_command_add_ignore(int node_id);
void handle_command_remove_ip_from_list(int iplist, ip_t* ip);
void handle_command_remove_ignore(int node_id);
void handle_command_reset_ignores();
void handle_command_add_name(int node_id, char* name);
void handle_command_block_data(int node_id);
void handle_command_stop_block_data(int node_id);
void handle_command_allow_data(int node_id);
void handle_command_stop_allow_data(int node_id);

#endif
