#ifndef SPIN_LIST_H
#define LIST_MAINLOOP_H

#define IPLIST_BLOCK		0
#define IPLIST_IGNORE		1
#define IPLIST_ALLOW		2
#define N_IPLIST		3

#define SF_ADD			0
#define SF_REM			1
#define N_SF			2

typedef void (*spinfunc)(void*, int listid, int addrem, ip_t *ip_addr);

void spin_register(char *name, spinfunc wf, void *arg, int list[N_IPLIST]);
#endif
