#ifndef SPIN_MAINLOOP_H
#define SPIN_MAINLOOP_H

typedef void (*workfunc)(int, int );

void mainloop_register(char *name, workfunc wf, int fd, int toval);
void mainloop_run();
#endif
