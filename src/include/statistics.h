/*
 * Statistics module include
 */

#define DO_SPIN_STATS   1

#if DO_SPIN_STATS
typedef enum {
    STAT_TOTAL,
    STAT_MAX,
    N_STAT
} spin_stat_type_t;

typedef struct spin_stat *stat_p;
typedef struct spin_stat {
    const char *        stat_module;        /* Group of counters */
    const char *        stat_name;          /* Name of counter */
    spin_stat_type_t     stat_type;          /* Type enum */
    int                 stat_value;
    int                 stat_count;
    stat_p              stat_next;
} spin_stat_t;

void spin_stat_val(stat_p, int);

#define STAT_CONCAT(x, y) x ## y
#define STAT_PREF _spin_stat_

#define STAT_MODULE(modulename) static const char STAT_CONCAT(STAT_PREF, modname)[] = #modulename ;

#define STAT_COUNTER(ctr, descr, type) static spin_stat_t STAT_CONCAT(STAT_PREF, ctr) = { STAT_CONCAT(STAT_PREF, modname), #descr, type }

#define STAT_VALUE(ctr, val) spin_stat_val(&STAT_CONCAT(STAT_PREF, ctr), val)

#else // DO_SPIN_STATS

#define STAT_MODULE(x)  ;
#define STAT_COUNTER(x, y, z)
#define STAT_VALUE(x, y)

#endif // DO_SPIN_STATS
