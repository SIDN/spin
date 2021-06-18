 
#include "mainloop.h"
#include "spindata.h"
#include "statistics.h"

#if DO_SPIN_STATS

void core2pubsub_publish_chan(char *, spin_data, int);

static void
statpub(stat_p sp) {
    char tpbuf[100];
    cJSON *statobj, *membobj;

    statobj = cJSON_CreateObject();
    if (statobj == 0) {
        return;
    }

    membobj = cJSON_CreateStringReference(sp->stat_module);
    cJSON_AddItemToObject(statobj, "module", membobj);

    membobj = cJSON_CreateStringReference(sp->stat_name);
    cJSON_AddItemToObject(statobj, "name", membobj);

    cJSON_AddNumberToObject(statobj, "type", sp->stat_type);
    cJSON_AddNumberToObject(statobj, "value", sp->stat_value);
    cJSON_AddNumberToObject(statobj, "count", sp->stat_count);

    sprintf(tpbuf, "SPIN/stat/%s/%s", sp->stat_module, sp->stat_name);

    core2pubsub_publish_chan(tpbuf, statobj, 1);

    cJSON_Delete(statobj);
}

static void
wf_stat(void * arg, int data, int timeout) {
    stat_p sp;

    if (timeout) {
        // What else
        for (sp = spin_stat_chain; sp->stat_module; sp = sp->stat_next) {
            if (sp->stat_lastpubcount != sp->stat_count) {
                statpub(sp);
                sp->stat_lastpubcount = sp->stat_count;
            }
        }
    }
}

void
spin_stat_start() {

    mainloop_register("Statistics", wf_stat, (void *) 0, 0, 30000, 1);
}

void
spin_stat_finish() {

}


#endif // DO_SPIN_STATS
