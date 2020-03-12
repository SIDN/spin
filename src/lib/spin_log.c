
#include <stdio.h>

#include "spin_log.h"

int use_syslog_ = 1;
int log_verbosity = 6;

void spin_log_init(int use_syslog, int verbosity, const char* ident) {
    log_verbosity = verbosity;
    if (!use_syslog) {
        use_syslog_ = 0;
        openlog(ident, 0, LOG_DAEMON);
    }
}

void spin_log_close() {
    closelog();
}

void spin_log(int level, const char* format, ...) {
    va_list arg;

    /* Write the error message */
    if (level > log_verbosity) {
        return;
    }

    va_start(arg, format);
    if (use_syslog_) {
        vsyslog(level, format, arg);
    } else {
        vprintf(format, arg);
    }
    va_end(arg);
}

void spin_vlog(int level, const char* format, va_list arg) {
    if (level > log_verbosity) {
        return;
    }
    vsyslog(level, format, arg);
}
