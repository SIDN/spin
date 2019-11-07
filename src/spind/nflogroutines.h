#ifndef SPIN_NFLOGROUTINES_H
#define SPIN_NFLOGROUTINES_H

#include <stdint.h>

typedef void (*nflogfunc)(void* arg, int af, int proto, uint8_t* payload, int payloadsize, uint8_t *src_addr, uint8_t *dest_addr, unsigned src_port, unsigned dest_port);

void nflogroutine_register(char *name, nflogfunc wf, void *arg, int group_number);
void nflogroutine_close(char* name);
void nflog_close_handle();
#endif
