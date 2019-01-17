#ifndef SPIN_MAINLOOP_H
#define SPIN_MAINLOOP_H

typedef void (*workfunc)(int, int );

void mainloop_register(workfunc wf, int fd, int toval);
void mainloop_run();
#endif
