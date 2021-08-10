#ifndef SPIN_CONFIG_H
#define SPIN_CONFIG_H
#include "config.h"
// if USE_UCI is defined we use UCI with section spin.spind
// if not we use a file /etc/spin/spind.conf

#if USE_UCI
#define UCI_SECTION_NAME "spin.spind"
#endif

#define CONFIG_FILE "/etc/spin/spind.conf"

void init_config();
void config_set_option(char*, char*);
int get_config_entries(const char* config_file, int must_exist);
void spinconfig_print_defaults();

int spinconfig_log_usesyslog();
int spinconfig_log_loglevel();
char *spinconfig_log_file();
char *spinconfig_pid_file();
char *spinconfig_pubsub_host();
int spinconfig_pubsub_port();
char *spinconfig_pubsub_websocket_host();
int spinconfig_pubsub_websocket_port();
char *spinconfig_pubsub_channel_traffic();
int spinconfig_pubsub_timeout();
int spinconfig_pubsub_omitnode();
int spinconfig_pubsub_run_mosquitto();
char *spinconfig_pubsub_run_password_file();
char* spinconfig_pubsub_run_pid_file();
int spinconfig_iptable_nflog_dns_group();
int spinconfig_iptable_queue_block();
int spinconfig_iptable_place_block();
char *spinconfig_iptable_debug();
// The time (in seconds) that node_cache entries
// are kept after they have last been seen
int spinconfig_node_cache_retain_time();
int spinconfig_dots_enabled();
int spinconfig_dots_log_only();
char *spinconfig_spinweb_pid_file();
char* spinconfig_spinweb_interfaces();
int spinconfig_spinweb_port();
char* spinconfig_spinweb_tls_certificate_file();
char* spinconfig_spinweb_tls_key_file();
char* spinconfig_spinweb_password_file();
#endif // SPIN_CONFIG_H
