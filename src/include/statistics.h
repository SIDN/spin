/*
 * Statistics module include
 */

typedef enum {
    STAT_TOTAL,
    STAT_MAX,
    N_STAT
} stat_type_t;

typedef struct spin_stat *stat_p;
typedef struct spin_stat {
    const char *    stat_module;        /* Group of counters */
    const char *    stat_name;          /* Name of counter */
    stat_type_t     stat_type;          /* Type enum */
    int             stat_value;
    int             stat_count;
    stat_p          stat_next;
} stat_t;

void stat_val(stat_p, int);
