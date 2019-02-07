#ifndef SPIN_NFQROUTINES_H
#define SPIN_NFQROUTINES_H

typedef void (*nfqrfunc)(void*, char*);

void nfqroutine_register(char *name, nfqrfunc wf, void *arg, int queue);
#endif
