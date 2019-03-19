#include <stdio.h>
#include <string.h>

#include "mainloop.h"
#include "statistics.h"

#if DO_SPIN_STATS

void pubsub_publish(char *, int, char *, int);

void wf_stat(void * arg, int data, int timeout) {
    stat_p sp;
    char tpbuf[100];
    char jsbuf[512];


    if (timeout) {
        // What else
        for (sp = spin_stat_chain; sp->stat_module; sp = sp->stat_next) {
            sprintf(jsbuf, "{ \"module\": \"%s\", \"name\": \"%s\", \"type\": %d, \"value\": %d, \"count\": %d }",
                sp->stat_module, sp->stat_name,
                sp->stat_type, sp->stat_value, sp->stat_count);
            sprintf(tpbuf, "SPIN/stat/%s/%s", sp->stat_module, sp->stat_name);
            pubsub_publish(tpbuf, strlen(jsbuf), jsbuf, 1);
        }
    }
}

void
spin_stat_start() {

    mainloop_register("Statistics", wf_stat, (void *) 0, 0, 30000);
}

void
spin_stat_finish() {

}


#endif // DO_SPIN_STATS
