
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "spin_log.h"

int use_syslog_ = 1;
int log_stdout = 0;
int log_verbosity = 6;
FILE* logfile = NULL;

void spin_log_init(int use_syslog, int log_stdout, const char* log_filename, int verbosity, const char* ident) {
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
}

void spin_log_close() {
    if (logfile) {
        fclose(logfile);
    }
    closelog();
}

void spin_log(int level, const char* format, ...) {
    va_list arg[3];
    int arg_count = 0;
    int arg_current = 0;

    /* Write the error message */
    if (level > log_verbosity) {
        return;
    }

    if (use_syslog_) {
        arg_count++;
    }
    if (log_stdout) {
        arg_count++;
    }
    if (logfile) {
        arg_count++;
    }

    va_start(arg[0], format);
    for (int i = 1; i < arg_count; ++i) {
        va_copy(arg[i], arg[0]);
    }

    if (use_syslog_) {
        vsyslog(level, format, arg[arg_current]);
        arg_current++;
    }
    if (log_stdout) {
        vprintf(format, arg[arg_current]);
        arg_current++;
    }
    if (logfile) {
        vfprintf(logfile, format, arg[arg_current]);
        arg_current++;
        fflush(logfile);
    }

    va_end(arg[0]);
    for (int i = 1; i < arg_count; ++i) {
        va_end(arg[i]);
    }
}

void spin_vlog(int level, const char* format, va_list arg) {
    if (level > log_verbosity) {
        return;
    }
    vsyslog(level, format, arg);
}
