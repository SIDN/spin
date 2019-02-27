#include <stdio.h>
#include <string.h>

#include "statistics.h"

static stat_t end = { 0 };
static stat_p stat_chain = &end;

static char buf[512];

void pubsub_publish(char *, char *, int);

void
stat_val(stat_p sp, int val) {

    if (sp->stat_next == 0) {
        // First time use
        sp->stat_next = stat_chain;
        stat_chain = sp;
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
    }
    sprintf(buf, "{ \"var\": \"%s.%s\", \"type\": %d, \"value\": %d, \"count\": %d }\n",
                sp->stat_module, sp->stat_name,
                sp->stat_type, sp->stat_value, sp->stat_count);
    pubsub_publish("SPIN/stat", buf, strlen(buf));
}
