#include <stdio.h>
#include <string.h>

#include "mainloop.h"
#include "statistics.h"

#if DO_SPIN_STATS

static spin_stat_t end = { 0 };
static stat_p stat_chain = &end;

void pubsub_publish(char *, int, char *, int);

void wf_stat(void * arg, int data, int timeout) {
    stat_p sp;
    char tpbuf[100];
    char jsbuf[512];


    if (timeout) {
        // What else
        for (sp = stat_chain; sp->stat_module; sp = sp->stat_next) {
            sprintf(jsbuf, "{ \"module\": \"%s\", \"name\": \"%s\", \"type\": %d, \"value\": %d, \"count\": %d }",
                sp->stat_module, sp->stat_name,
                sp->stat_type, sp->stat_value, sp->stat_count);
            sprintf(tpbuf, "SPIN/stat/%s/%s", sp->stat_module, sp->stat_name);
            pubsub_publish(tpbuf, strlen(jsbuf), jsbuf, 1);
        }
    }
}

static void
firstuse() {

    mainloop_register("Statistics", wf_stat, (void *) 0, 0, 30000);
}

void
spin_stat_val(stat_p sp, int val) {
    static int inited=0;

    if (sp->stat_next == 0) {
        // First time use

        // Prepend to list, currently in reverse chronological order
        // Perhaps TODO, although UI should solve this
        sp->stat_next = stat_chain;
        stat_chain = sp;
        if (!inited) {
            firstuse();
            inited = 1;
        }
    }
    sp->stat_count++;
    switch (sp->stat_type) {
    case STAT_TOTAL:
        sp->stat_value += val;
        break;
    case STAT_MAX:
        if (val > sp->stat_value) {
            sp->stat_value = val;
        }
        break;
    case N_STAT:
        // should not happen.
        break;
    }
}

#endif // DO_SPIN_STATS
