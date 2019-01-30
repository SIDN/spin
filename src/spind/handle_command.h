#ifndef HANDLE_H
#define HANDLE_H 1

void handle_command_get_list(config_command_t cmd, const char* json_command);
void handle_command_add_filter(int node_id);
void handle_command_remove_ip(config_command_t cmd, ip_t* ip);
void handle_command_reset_filters();
void handle_command_add_name(int node_id, char* name);
void handle_command_block_data(int node_id);
void handle_command_stop_block_data(int node_id);
void handle_command_allow_data(int node_id);
void handle_command_stop_allow_data(int node_id);

#endif
