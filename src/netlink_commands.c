
#include "netlink_commands.h"

netlink_command_result_t* netlink_command_result_create(void) {
    netlink_command_result_t* command_result = (netlink_command_result_t*)malloc(sizeof(netlink_command_result_t));
    command_result->ip_max = 10;
    command_result->ips = malloc(sizeof(ip_t) * command_result->ip_max);
    command_result->ip_count = 0;
    command_result->error = NULL;

    return command_result;
}

void netlink_command_result_destroy(netlink_command_result_t* command_result) {
    free(command_result->ips);
    free(command_result);
}

void netlink_command_result_add_ip(netlink_command_result_t* command_result, uint8_t ip_fam, uint8_t* ip) {
    //uint8_t* s_ip;
    if (command_result->ip_count + 1 >= command_result->ip_max) {
        command_result->ip_max = command_result->ip_max * 2;
        command_result->ips = realloc(command_result->ips, sizeof(ip_t) * command_result->ip_max);
    }
    // we store it in the sizeof(ip_t)-bit 'format' of spin here
    if (ip_fam == AF_INET) {
        command_result->ips[command_result->ip_count].family = ip_fam;
        memset(command_result->ips[command_result->ip_count].addr, 0, 12);
        memcpy(&command_result->ips[command_result->ip_count].addr[12], ip, 4);
        command_result->ip_count++;
    } else if (ip_fam == AF_INET6) {
        command_result->ips[command_result->ip_count].family = ip_fam;
        memcpy(command_result->ips[command_result->ip_count].addr, ip, 16);
        command_result->ip_count++;
    } else {
        printf("[XX] error: unknown ip version\n");
    }
}

void netlink_command_result_set_error(netlink_command_result_t* command_result, char* error) {
    command_result->error = error;
}
