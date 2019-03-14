// Workaround for statistics code being too tightly coupled with spind at this moment
// Include this file to disable statistics
#define SPIN_STATISTICS_H 1

#define STAT_MODULE(a)
#define STAT_VALUE(a,b) {}
#define STAT_COUNTER(a,b,c)

void spin_stat_val() {}
