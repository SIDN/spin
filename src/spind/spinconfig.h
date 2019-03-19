// if USE_UCI is defined we use UCI with section spin.spind
// if not we use a file /etc/spin/spind.conf

#if USE_UCI
#define UCI_SECTION_NAME "spin.spind"
#else
#define CONFIG_FILE "/etc/spin/spind.conf"
#endif

void init_config();
void config_set_option(char*, char*);

int spinconfig_log_usesyslog();
int spinconfig_log_loglevel();
char *spinconfig_pubsub_host();
int spinconfig_pubsub_port();
char *spinconfig_pubsub_channel_commands();
char *spinconfig_pubsub_channel_traffic();
int spinconfig_pubsub_timeout();
int spinconfig_pubsub_omitnode();
int spinconfig_iptable_nflog_dns_group();
int spinconfig_iptable_queue_block();
int spinconfig_iptable_place_dns();
int spinconfig_iptable_place_block();
char *spinconfig_iptable_debug();
// The time (in seconds) that node_cache entries
// are kept after they have last been seen
int spinconfig_node_cache_retain_time();

