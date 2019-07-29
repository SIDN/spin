#ifndef SPIN_NFQROUTINES_H
#define SPIN_NFQROUTINES_H

typedef int (*nfqrfunc)(void* arg, int af, int proto, uint8_t* payload, int payloadsize, uint8_t *src_addr, uint8_t *dest_addr, unsigned src_port, unsigned dest_port);

void nfqroutine_register(char *name, nfqrfunc wf, void *arg, int queue);
void nfqroutine_close(char* name);
void nfq_close_handle();
#endif
