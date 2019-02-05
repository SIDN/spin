int init_netlink(int, node_cache_t*);
void cleanup_cache();
void cleanup_netlink();

int core2kernel_do(config_command_t);
int core2kernel_do_ip(config_command_t, ip_t*);
