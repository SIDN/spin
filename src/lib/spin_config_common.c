#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "spin_log.h"
#include "spin_config.h"

enum configs {
    LOG_USESYSLOG,
    LOG_LOGLEVEL,
    LOG_FILE,
    PID_FILE,
    PUBSUB_HOST,
    PUBSUB_PORT,
    PUBSUB_WEBSOCKET_HOST,
    PUBSUB_WEBSOCKET_PORT,
    PUBSUB_CHANNEL_TRAFFIC,
    PUBSUB_TIMEOUT,
    PUBSUB_RUN_MOSQUITTO,
    PUBSUB_RUN_PASSWORD_FILE,
    PUBSUB_RUN_PID_FILE,
    IPTABLE_QUEUE_DNS,
    IPTABLE_QUEUE_BLOCK,
    IPTABLE_PLACE_DNS,
    IPTABLE_PLACE_BLOCK,
    IPTABLE_DEBUG,
    NODE_CACHE_RETAIN_TIME,
    DOTS_ENABLED,   // Enable DOTS handler functionality
    DOTS_LOG_ONLY, // Only LOG DOTS mitigation request matches (do not block them)
    SPINWEB_PID_FILE,
    SPINWEB_INTERFACES,
    SPINWEB_PORT,
    SPINWEB_TLS_CERTIFICATE_FILE,
    SPINWEB_TLS_KEY_FILE,
    SPINWEB_PASSWORD_FILE,
};

struct conf_item {
    char *ci_name;
    char *ci_default;
    char *ci_value;
} ci_list[] = {
    [LOG_USESYSLOG] =
            { "log_usesyslog",              "1",                0   },
    [LOG_LOGLEVEL] =
            { "log_loglevel",               "6",                0   },
    [LOG_FILE] =
            { "log_file",                   "",                 0   },
    [PID_FILE] =
            { "pid_file",                   "",                 0   },
    [PUBSUB_HOST] =
            { "pubsub_host",                "127.0.0.1",        0   },
    [PUBSUB_PORT] =
            { "pubsub_port",                "1883",             0   },
    [PUBSUB_WEBSOCKET_HOST] =
            { "pubsub_websocket_host",      "127.0.0.1",        0   },
    [PUBSUB_WEBSOCKET_PORT] =
            { "pubsub_websocket_port",      "1884",             0   },
    [PUBSUB_CHANNEL_TRAFFIC] =
            { "pubsub_channel_traffic",     "SPIN/traffic",     0   },
    [PUBSUB_TIMEOUT] =
            { "pubsub_timeout",             "60",               0   },
    [PUBSUB_RUN_MOSQUITTO] =
            { "pubsub_run_mosquitto",       "1",                0   },
    [PUBSUB_RUN_PASSWORD_FILE] =
            { "pubsub_run_password_file",   "",                 0   },
    [PUBSUB_RUN_PID_FILE] =
            { "pubsub_run_pid_file",        "",                 0   },
    [IPTABLE_QUEUE_DNS] =
            { "iptable_queue_dns",          "1",                0   },
    [IPTABLE_QUEUE_BLOCK] =
            { "iptable_queue_block",        "2",                0   },
    [IPTABLE_PLACE_DNS] =
            { "iptable_place_dns",          "0",                0   },
    [IPTABLE_PLACE_BLOCK] =
            { "iptable_place_block",        "0",                0   },
    [IPTABLE_DEBUG] =
            { "iptable_debug",           "/tmp/block_commands", 0   },
    [NODE_CACHE_RETAIN_TIME] =
            { "node_cache_retain_time",     "1800",             0   },
    [DOTS_ENABLED] =
            { "dots_enabled",               "0",                0   },
    [DOTS_LOG_ONLY] =
            { "dots_log_only",              "0",                0   },
    [SPINWEB_PID_FILE] =
            { "spinweb_pid_file",           "",                 0   },
    [SPINWEB_INTERFACES] =
            { "spinweb_interfaces",          "127.0.0.1",       0   },
    [SPINWEB_PORT] =
            { "spinweb_port",                "13026",           0   },
    [SPINWEB_TLS_CERTIFICATE_FILE] =
            { "spinweb_tls_certificate_file", "",               0   },
    [SPINWEB_TLS_KEY_FILE] =
            { "spinweb_tls_key_file",         "",               0   },
    [SPINWEB_PASSWORD_FILE] =
            { "spinweb_password_file",        "",               0   },
 { 0, 0, 0 }
};

void
config_set_option(char *name, char *value) {
    struct conf_item *p;

    // printf("Set option %s to value '%s'\n", name, value);
    for (p=ci_list; p->ci_name != 0; p++) {
        if (strcmp(p->ci_name, name)==0) {
            p->ci_value = value;
            return;
        }
    }
    spin_log(LOG_ERR, "Found unknown option %s, value %s\n", name, value);
}

void
init_config(const char* config_file, int must_exist) {
    struct conf_item *p;
    //int get_config_entries(const char* config_file, int must_exist);

    if (get_config_entries(config_file, must_exist)) {
        spin_log(LOG_ERR, " init_config GCE failed\n");
    }

    // Set rest to default
    for (p=ci_list; p->ci_name != 0; p++) {
        if (p->ci_value == 0) {
            p->ci_value = p->ci_default;
        }
    }
}

static int
spi_int(int n) {
    int val;

    val = atoi(ci_list[n].ci_value);
    // printf("spi_int(%d) = %d('%s')\n", n, val, ci_list[n].ci_value);
    return val;
}

static char *
spi_str(int n) {
    char *val;

    val = ci_list[n].ci_value;
    // printf("spi_str(%d) = '%s'\n", n, val);
    return val;
}

int spinconfig_log_usesyslog() {

    return (spi_int(LOG_USESYSLOG));
}

int spinconfig_log_loglevel() {

    return (spi_int(LOG_LOGLEVEL));
}

char* spinconfig_log_file() {

    return (spi_str(LOG_FILE));
}

char* spinconfig_pid_file() {
    return (spi_str(PID_FILE));
}

char *spinconfig_pubsub_host() {

    return(spi_str(PUBSUB_HOST));
}

int spinconfig_pubsub_port() {

    return(spi_int(PUBSUB_PORT));
}

char *spinconfig_pubsub_websocket_host() {
    return(spi_str(PUBSUB_WEBSOCKET_HOST));
}

int spinconfig_pubsub_websocket_port() {
    return(spi_int(PUBSUB_WEBSOCKET_PORT));
}

char *spinconfig_pubsub_channel_traffic() {

    return(spi_str(PUBSUB_CHANNEL_TRAFFIC));
}

int spinconfig_pubsub_timeout() {

    return(spi_int(PUBSUB_TIMEOUT));
}

int spinconfig_pubsub_run_mosquitto() {
    return(spi_int(PUBSUB_RUN_MOSQUITTO));
}

char* spinconfig_pubsub_run_password_file() {
    return(spi_str(PUBSUB_RUN_PASSWORD_FILE));
}

char* spinconfig_pubsub_run_pid_file() {
    return(spi_str(PUBSUB_RUN_PID_FILE));
}

int spinconfig_iptable_nflog_dns_group() {
    return(spi_int(IPTABLE_QUEUE_DNS));
}

int spinconfig_iptable_queue_block() {
    return(spi_int(IPTABLE_QUEUE_BLOCK));
}

int spinconfig_iptable_place_dns() {
    return(spi_int(IPTABLE_PLACE_DNS));
}

int spinconfig_iptable_place_block() {
    return(spi_int(IPTABLE_PLACE_BLOCK));
}

char *spinconfig_iptable_debug() {
    return(spi_str(IPTABLE_DEBUG));
}

int spinconfig_node_cache_retain_time() {
    return(spi_int(NODE_CACHE_RETAIN_TIME));
}

int spinconfig_dots_enabled() {
    return(spi_int(DOTS_ENABLED));
}

int spinconfig_dots_log_only() {
    return(spi_int(DOTS_LOG_ONLY));
}


char* spinconfig_spinweb_pid_file() {
    return (spi_str(SPINWEB_PID_FILE));
}

char* spinconfig_spinweb_interfaces() {
    return(spi_str(SPINWEB_INTERFACES));
}

int spinconfig_spinweb_port() {
    return(spi_int(SPINWEB_PORT));
}

char* spinconfig_spinweb_tls_certificate_file() {
    return(spi_str(SPINWEB_TLS_CERTIFICATE_FILE));
}

char* spinconfig_spinweb_tls_key_file() {
    return(spi_str(SPINWEB_TLS_KEY_FILE));
}

char* spinconfig_spinweb_password_file() {
    return(spi_str(SPINWEB_PASSWORD_FILE));
}

void spinconfig_print_defaults() {
    struct conf_item* ci;
    int i = 0;
    ci = &ci_list[0];
    while (ci->ci_name != 0) {
        ci = &ci_list[i++];
        if (ci->ci_name != NULL) {
            printf("%s = %s\n", ci->ci_name, ci->ci_default);
        }
    }
}
