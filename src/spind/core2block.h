void init_core2block();
void cleanup_core2block();

void c2b_changelist(void* arg, int iplist, int add, ip_t *ip_addr);
void c2b_node_updated(node_t *node);
void c2b_node_persistent_start(int nodenum);
void c2b_node_persistent_end(int nodenum);
void c2b_node_ipaddress(int nodenum, ip_t *ip_addr);
void c2b_blockflow_start(int nodenum1, int nodenum2);
void c2b_blockflow_end(int nodenum1, int nodenum2);

