/*
 * Statistics module include
 */
#ifndef SPIN_STATISTICS_H
#define SPIN_STATISTICS_H 1

/*
 * DO_SPIN_STATS
 * 
 * 0: no statistics
 * 1: statistics runtime in kernel
 * 2: statistics at end for unit test
 */

#ifndef DO_SPIN_STATS
#define DO_SPIN_STATS   1
#endif

#if DO_SPIN_STATS != 0

#define SPIN_STAT_START()   spin_stat_start();
#define SPIN_STAT_FINISH()   spin_stat_finish();

void spin_stat_start();
void spin_stat_finish();

typedef enum {
    STAT_TOTAL,
    STAT_MAX,
    N_STAT
} spin_stat_type_t;

typedef struct spin_stat *stat_p;
typedef struct spin_stat {
    const char *        stat_module;        /* Group of counters */
    const char *        stat_name;          /* Name of counter */
    spin_stat_type_t    stat_type;          /* Type enum */
    int                 stat_value;
    int                 stat_count;
    int                 stat_lastpubcount;  /* Count when last published */
    stat_p              stat_next;
} spin_stat_t;

extern spin_stat_t spin_stat_end;
extern stat_p spin_stat_chain;

void spin_stat_val(stat_p, int);

#define STAT_CONCAT(x, y) x ## y
#define STAT_PREF _spin_stat_

#define STAT_MODULE(modulename) static const char STAT_CONCAT(STAT_PREF, modname)[] = #modulename ;

#define STAT_COUNTER(ctr, descr, type) static spin_stat_t STAT_CONCAT(STAT_PREF, ctr) = { STAT_CONCAT(STAT_PREF, modname), #descr, type, 0, 0, 0, NULL }

#define STAT_VALUE(ctr, val) spin_stat_val(&STAT_CONCAT(STAT_PREF, ctr), val)

#else // DO_SPIN_STATS

#define SPIN_STAT_START()
#define SPIN_STAT_FINISH()

#define STAT_MODULE(x)  ;
#define STAT_COUNTER(x, y, z)
#define STAT_VALUE(x, y)

#endif // DO_SPIN_STATS

#endif // SPIN_STATISTICS_H
