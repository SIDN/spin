#ifndef SPIN_LIST_H
#define SPIN_LIST_H

enum iplist_index {
    IPLIST_BLOCK,
    IPLIST_IGNORE,
    IPLIST_ALLOW,
    N_IPLIST
};

enum spinfunc_command {
    SF_ADD,
    SF_REM,
    N_SF
};

typedef void (*spinfunc)(void*, int listid, int addrem, ip_t *ip_addr);

void spin_register(char *name, spinfunc wf, void *arg, int list[N_IPLIST]);
#endif
