#include <string.h>
#include <stdlib.h>

#include "config.h"
#include "spin_log.h"
#include "spinconfig.h"


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

int get_config_entries() {
    struct uci_context *c;
    struct uci_ptr ptr;
    struct uci_element *e;
    char buf[100];

    // fprintf(stderr, "Reading configuration from UCI section %s\n", UCI_SECTION_NAME);

    c = uci_alloc_context();

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
#include <ctype.h>

#define MAXLINE 200

int get_config_entries() {
    FILE *conf_file;
    char line[MAXLINE];
    char *beginofkeyw, *endofkeyw;
    char *beginofvalue, *endofvalue;
    char *equalsptr;

    fprintf(stderr, "Read configuration from %s\n", CONFIG_FILE);

    conf_file = fopen(CONFIG_FILE, "r");
    if (conf_file == 0) {
        spin_log(LOG_INFO, "Could not open %s\n", CONFIG_FILE);
        return 1;
    }

    while (fgets(line, MAXLINE, conf_file) != 0) {
        if (line[0] == '#' || line[0] == '\n') {
            continue;
        }

        equalsptr = strchr(line, '=');

        beginofkeyw = line;

        while(isspace(beginofkeyw[0])) {
            beginofkeyw++;
        }

        endofkeyw = equalsptr -1;
        if (endofkeyw < line) {
            spin_log(LOG_ERR, "Conf line starts with =\n");
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
            spin_log(LOG_ERR, "Very long conf line\n");
            continue;
        }
        while (isspace(endofvalue[0])) {
            endofvalue--;
        }

        if (endofvalue <= endofkeyw) {
            spin_log(LOG_ERR, "No conf value found\n");
            continue;
        }

        endofvalue[1] = 0;
        config_set_option(beginofkeyw, strdup(beginofvalue));
    }
    return 0;
}

#endif
