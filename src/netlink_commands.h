#ifndef SPIN_NETLINK_COMMANDS_H
#define SPIN_NETLINK_COMMANDS_H 1

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <arpa/inet.h>
#include "util.h"

typedef struct {
    ip_t* ips;
    size_t ip_count;
    size_t ip_max;
    char* error;
} netlink_command_result_t;



netlink_command_result_t* netlink_command_result_create(void);
void netlink_command_result_destroy(netlink_command_result_t* command_result);
void netlink_command_result_add_ip(netlink_command_result_t* command_result, uint8_t ip_fam, uint8_t* ip);
void netlink_command_result_set_error(netlink_command_result_t* command_result, char* error);

#endif // SPIN_NETLINK_COMMANDS_H
