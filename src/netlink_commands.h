#ifndef SPIN_NETLINK_COMMANDS_H
#define SPIN_NETLINK_COMMANDS_H 1

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <arpa/inet.h>
#include "util.h"

#include "spin_cfg.h"


typedef struct {
    ip_t* ips;
    size_t ip_count;
    size_t ip_max;
    char* error;
} netlink_command_result_t;

netlink_command_result_t* netlink_command_result_create(void);
void netlink_command_result_destroy(netlink_command_result_t* command_result);
void netlink_command_result_add_ip(netlink_command_result_t* command_result, uint8_t ip_fam, uint8_t* ip);
int netlink_command_result_contains_ip(netlink_command_result_t* command_result, ip_t* ip);
void netlink_command_result_set_error(netlink_command_result_t* command_result, char* error);

netlink_command_result_t* send_netlink_command_buf(size_t cmdbuf_size, unsigned char* cmdbuf);
netlink_command_result_t* send_netlink_command_noarg(config_command_t cmd);
netlink_command_result_t* send_netlink_command_iparg(config_command_t cmd, uint8_t* ip);


#endif // SPIN_NETLINK_COMMANDS_H
