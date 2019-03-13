#ifndef HANDLE_H
#define HANDLE_H 1

void handle_command_get_iplist(int iplist, const char* json_command);
void handle_command_remove_ip_from_list(int iplist, ip_t* ip);
void handle_command_reset_ignores();
void handle_command_add_name(int node_id, char* name);
void handle_list_membership(int listid, int addrem, int node_id);

unsigned int create_mqtt_command(buffer_t*, const char*, char*, char*);

#endif
