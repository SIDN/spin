#include <errno.h>
#include <string.h>
#include <sys/stat.h>

#include "config.h"
#include "spin_config.h"
#include "spin_log.h"

#if USE_UCI
#include <uci.h>

/*
 * WARNING!!
 *
 * UCI code treats pointers to paths as writable buffers
 * do *NOT* pass constant strings to them
 *
 */

static void uci_report_option(struct uci_option *o)
{

    if (o->type != UCI_TYPE_STRING) {
        spin_log(LOG_ERR, "Option %s is not string\n", o->e.name);
        return;
    }
    config_set_option(o->e.name, strdup(o->v.string));
}

static void uci_report_section(struct uci_section *s)
{
    struct uci_element *e;

    uci_foreach_element(&s->options, e) {
        uci_report_option(uci_to_option(e));
    }
}

int get_config_entries(const char* config_file, int must_exist) {
    struct uci_context *c;
    struct uci_ptr ptr;
    struct uci_element *e;
    char buf[100];


    c = uci_alloc_context();

    spin_log(LOG_INFO, "Reading config from UCI\n");

    if (config_file != NULL && must_exist) {
        spin_log(LOG_WARNING, "UCI Mode enabled, config file %s ignored\n", config_file);
    }

    strcpy(buf, UCI_SECTION_NAME);  // IMPORTANT
    if (uci_lookup_ptr(c, &ptr, buf, true) != UCI_OK) {
        spin_log(LOG_ERR, "UCI lookup spin.spind failed");
        return 1;
    }
    e = ptr.last;
    if (e->type != UCI_TYPE_SECTION) {
        spin_log(LOG_ERR, "UCI type is not SECTION");
        return 1;
    }
    uci_report_section(ptr.s);
    uci_free_context (c);
    return 0;
}

#else /* USE_UCI */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#define MAXLINE 200

void
config_read_error(int abort, const char* format, ...) {
    va_list arg;
    va_start(arg, format);
    vfprintf(stderr, format, arg);
    va_end(arg);
    va_start(arg, format);
    spin_vlog(LOG_ERR, format, arg);
    va_end(arg);

    if (abort) {
        fprintf(stderr, "aborting spind startup\n");
        spin_log(LOG_ERR, "aborting spind startup\n");
        exit(1);
    }
}

int get_config_entries(const char* config_file, int must_exist) {
    FILE *conf_file;
    char line[MAXLINE];
    char *beginofkeyw, *endofkeyw;
    char *beginofvalue, *endofvalue;
    char *equalsptr;
    struct stat file_stat;

    fprintf(stderr, "[XX] opening file: %s\n", config_file);
    if (stat(config_file, &file_stat) != 0) {
        config_read_error(must_exist, "Config file %s does not exist\n", config_file);
        return 1;
    }
    if (!S_ISREG(file_stat.st_mode)) {
        config_read_error(1, "Config file %s does not appear to be a file, aborting\n", config_file);
        return 1;
    }
    conf_file = fopen(config_file, "r");
    if (conf_file == 0) {
        config_read_error(1, "Could not open %s: %s\n", config_file, strerror(errno));
        return 1;
    }

    while (fgets(line, MAXLINE, conf_file) != 0) {
        if (line[0] == '#' || line[0] == '\n') {
            continue;
        }

        equalsptr = strchr(line, '=');
        if (equalsptr == NULL) {
            config_read_error(1, "Bad config line, no = character\n");
            continue;
        }

        beginofkeyw = line;

        while(isspace(beginofkeyw[0])) {
            beginofkeyw++;
        }

        endofkeyw = equalsptr -1;
        if (endofkeyw < line) {
            config_read_error(1, "Conf line starts with =\n");
            continue;
        }
        while (endofkeyw > line && isspace(endofkeyw[0])) {
            endofkeyw--;
        }
        endofkeyw[1] = 0;   /* Terminate keyword */

        beginofvalue = equalsptr +1;
        while (isspace(beginofvalue[0])) {
            beginofvalue++;
        }

        endofvalue = strchr(beginofvalue, '\n');
        if (endofvalue == 0) {
            endofvalue = strchr(beginofvalue, EOF);
            if (endofvalue == 0) {
                config_read_error(1, "Very long conf line\n");
                continue;
            }
        }
        while (isspace(endofvalue[0])) {
            endofvalue--;
        }

        if (endofvalue <= endofkeyw) {
            config_read_error(1, "No conf value found\n");
            continue;
        }

        endofvalue[1] = 0;
        config_set_option(beginofkeyw, strdup(beginofvalue));
    }
    return 0;
}

#endif
