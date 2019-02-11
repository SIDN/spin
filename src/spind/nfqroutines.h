#ifndef SPIN_NFQROUTINES_H
#define SPIN_NFQROUTINES_H

typedef int (*nfqrfunc)(void* arg, int af, int proto, char* payload, int payloadsize, uint8_t *src_addr, uint8_t *dest_addr, unsigned src_port, unsigned dest_port);

void nfqroutine_register(char *name, nfqrfunc wf, void *arg, int queue);
#endif
