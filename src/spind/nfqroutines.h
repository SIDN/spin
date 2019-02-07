#ifndef SPIN_NFQROUTINES_H
#define SPIN_NFQROUTINES_H

typedef int (*nfqrfunc)(void*, int, char*, int);

void nfqroutine_register(char *name, nfqrfunc wf, void *arg, int queue);
#endif
