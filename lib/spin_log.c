
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "spin_log.h"

int use_syslog_ = 1;
int log_stdout = 0;
int log_verbosity = 6;
FILE* logfile = NULL;

void spin_log_init(int use_syslog, int log_stdout_a, const char* log_filename, int verbosity, const char* ident) {
    log_verbosity = verbosity;
    if (log_filename && strlen(log_filename) > 0) {
        if (logfile) {
            fclose(logfile);
            logfile = NULL;
        }
        logfile = fopen(log_filename, "a");
        if (!logfile) {
            fprintf(stderr, "Error opening logfile %s: %s\n", log_filename, strerror(errno));
        }
    }
    if (use_syslog) {
        use_syslog_ = use_syslog;
        openlog(ident, 0, LOG_DAEMON);
    }
    log_stdout = log_stdout_a;
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

    if (use_syslog_) {
        va_start(arg, format);
        vsyslog(level, format, arg);
        va_end(arg);
    }
    if (log_stdout) {
        va_start(arg, format);
        vprintf(format, arg);
        va_end(arg);
    }
    if (logfile) {
        va_start(arg, format);
        vfprintf(logfile, format, arg);
        fflush(logfile);
        va_end(arg);
    }
}

void spin_vlog(int level, const char* format, va_list arg) {
    if (level > log_verbosity) {
        return;
    }
    vsyslog(level, format, arg);
}
