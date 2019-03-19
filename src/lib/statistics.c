#include "statistics.h"

/*
 * Routine to do statistics
 */

spin_stat_t spin_stat_end = { 0 };
stat_p spin_stat_chain = &spin_stat_end;

void
spin_stat_val(stat_p sp, int val) {

    if (sp->stat_next == 0) {
        // First time use

        // Prepend to list, currently in reverse chronological order
        // Perhaps TODO, although UI should solve this
        sp->stat_next = spin_stat_chain;
        spin_stat_chain = sp;
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

#if DO_SPIN_STATS == 2
#include <stdio.h>

spin_stat_start() {

}

spin_stat_finish() {
    spin_stat_t *sp;

    for (sp = spin_stat_chain; sp->stat_module; sp = sp->stat_next) {
        fprintf(stderr, "{ \"module\": \"%s\", \"name\": \"%s\", \"type\": %d, \"value\": %d, \"count\": %d }\n",
            sp->stat_module, sp->stat_name,
            sp->stat_type, sp->stat_value, sp->stat_count);
    }
}

#endif
