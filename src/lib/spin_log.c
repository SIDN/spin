
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "spin_log.h"

int use_syslog_ = 1;
int log_verbosity = 6;
FILE* logfile = NULL;

void spin_log_init(int use_syslog, const char* filename, int verbosity, const char* ident) {
    log_verbosity = verbosity;
    if (!use_syslog) {
        if (filename) {
            if (logfile) {
                fclose(logfile);
                logfile = NULL;
            }
            logfile = fopen(filename, "a");
            if (!logfile) {
                fprintf(stderr, "Error opening logfile %s: %s\n", filename, strerror(errno));
            }
        }
        use_syslog_ = 0;
        openlog(ident, 0, LOG_DAEMON);
    }
}

void spin_log_close() {
    if (logfile) {
        fclose(logfile);
    }
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
    } else if (logfile) {
        vfprintf(logfile, format, arg);
        fflush(logfile);
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
