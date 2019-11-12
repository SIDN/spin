#ifndef SPIN_LOG_H
#define SPIN_LOG_H 1

#include <syslog.h>
#include <stdarg.h>

// Initialize logging; set logger to level,
// and choose between syslog (default) or logging to stdout
void spin_log_init(int use_syslog, int verbosity, const char* ident);

void spin_log_close();

void spin_log(int level, const char* format, ...) __attribute__((__format__ (printf, 2, 3)));

void spin_vlog(int level, const char* format, va_list arg);

#endif //SPIN_LOG_H
