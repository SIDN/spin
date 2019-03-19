#include <stdlib.h>
#include <mosquitto.h>

#include "node_cache.h"
#include "jsmn.h"
#include "util.h"
#include "mainloop.h"
#include "spinconfig.h"
#include "spin_log.h"
#include "spin_list.h"
#include "handle_command.h"
#include "statistics.h"

static char *mqtt_channel_traffic;
static char *mqtt_channel_commands;
static int mosquitto_keepalive_time;

struct mosquitto* mosq;
extern node_cache_t* node_cache;

STAT_MODULE(pubsub)

/*
 * Code that pushes gathered results back to Spin traffic channel for
 * UI code(and others?) to do their work with
 */

void
pubsub_publish(char *channel, int payloadlen, const void* payload) {

    /*
     * There is a result from command, but for now ignored
     */
    mosquitto_publish(mosq, NULL, channel, payloadlen, payload, 0, false);
}

void core2pubsub_publish(buffer_t *buf) {
    STAT_COUNTER(ctr, traffic-publish, STAT_TOTAL);

    STAT_VALUE(ctr, buffer_size(buf));
    pubsub_publish(mqtt_channel_traffic, buffer_size(buf), buffer_str(buf));
}

/* End push back code */

/* (Re)start command Mosquitto server only */

void send_command_restart() {
    buffer_t* response_json = buffer_create(4096);
    create_mqtt_command(response_json, "serverRestart", NULL, NULL);
    buffer_finish(response_json);
    if (buffer_ok(response_json)) {
        core2pubsub_publish(response_json);
    } else {
        spin_log(LOG_ERR, "error: response size too large\n");
    }
    buffer_destroy(response_json);
}

// returns 1 on success, 0 on error
static int
json_parse_int_arg(int* dest,
                       const char* json_str,
                       jsmntok_t* tokens,
                       int argument_token_i) {
    jsmntok_t token = tokens[argument_token_i];

    if (token.type != JSMN_PRIMITIVE)
        return 0;
    *dest = atoi(json_str + token.start);
    return 1;
}

static int
json_parse_string_arg(char* dest,
                          size_t dest_size,
                          const char* json_str,
                          jsmntok_t* tokens,
                          int argument_token_i) {
    jsmntok_t token = tokens[argument_token_i];
    if (token.type != JSMN_STRING)
        return 0;
    snprintf(dest, dest_size, "%.*s", token.end - token.start, json_str+token.start);
    return 1;
}

static int
json_parse_ip_arg(ip_t* dest,
                      const char* json_str,
                      jsmntok_t* tokens,
                      int argument_token_i) {
    char ip_str[INET6_ADDRSTRLEN];

    if (!json_parse_string_arg(ip_str, INET6_ADDRSTRLEN,
                        json_str, tokens, argument_token_i))
        return 0;
    if(!spin_pton(dest, ip_str))
        return 0;
    return 1;
}

// more complex argument, of the form
// { "node_id": <int>, "name": <str> }
static int
json_parse_node_id_name_arg(int* node_id,
                                char* name,
                                size_t name_size,
                                const char* json_str,
                                jsmntok_t* tokens,
                                int argument_token_i) {
    jsmntok_t token = tokens[argument_token_i];
    int i, i_n, i_v, id_found = 0, name_found = 0;

    if (token.type != JSMN_OBJECT || token.size != 2)
        return 0;
    for (i = 0; i < token.size; i++) {
        // i*2 is name, i*2 + 1 is value
        i_n = argument_token_i + i*2 + 1;
        i_v = argument_token_i + i*2 + 2;
        if (strncmp("name", json_str + tokens[i_n].start, tokens[i_n].end-tokens[i_n].start) == 0) {
            if(!json_parse_string_arg(name, name_size, json_str, tokens, i_v))
                return 0;
            name_found = 1;
        } else if (strncmp("node_id", json_str + tokens[i_n].start, tokens[i_n].end-tokens[i_n].start) == 0) {
            if(!json_parse_int_arg(node_id, json_str, tokens, i_v))
                return 0;
            id_found = 1;
        }
    }
    return (name_found && id_found);
}

/*
 * All commands implemented by Pub/Sub interface
 */

// Commands
#define PSC_V_ADD               SF_ADD
#define PSC_V_REM               SF_REM
#define PSC_V_REM_IP            N_SF + 0
#define PSC_V_RESET             N_SF + 1
#define PSC_V_GET               N_SF + 2

// Types
#define PSC_O_BLOCK             IPLIST_BLOCK
#define PSC_O_IGNORE            IPLIST_IGNORE
#define PSC_O_ALLOW             IPLIST_ALLOW
#define PSC_O_NAME              N_IPLIST + 0

#define STR_AND_LEN(s)  s, (sizeof(s)-1)

static struct pubsub_commands {
    char *      psc_commandstr;         // String of command
    int         psc_commandlen;         // Length of command
    int         psc_verb;               // Verb
    int         psc_object;             // Object
} pubsub_commands[] = {
    { STR_AND_LEN("get_blocks"),        PSC_V_GET,      PSC_O_BLOCK},
    { STR_AND_LEN("get_ignores"),       PSC_V_GET,      PSC_O_IGNORE},
    { STR_AND_LEN("get_alloweds"),      PSC_V_GET,      PSC_O_ALLOW},
    { STR_AND_LEN("get_names"),         PSC_V_GET,      PSC_O_NAME },
    { STR_AND_LEN("add_block_node"),    PSC_V_ADD,      PSC_O_BLOCK},
    { STR_AND_LEN("add_ignore_node"),   PSC_V_ADD,      PSC_O_IGNORE},
    { STR_AND_LEN("add_allow_node"),    PSC_V_ADD,      PSC_O_ALLOW},
    { STR_AND_LEN("add_name"),          PSC_V_ADD,      PSC_O_NAME},
    { STR_AND_LEN("remove_block_node"), PSC_V_REM,      PSC_O_BLOCK},
    { STR_AND_LEN("remove_ignore_node"),PSC_V_REM,      PSC_O_IGNORE},
    { STR_AND_LEN("remove_allow_node"), PSC_V_REM,      PSC_O_ALLOW},
    { STR_AND_LEN("remove_block_ip"),   PSC_V_REM_IP,   PSC_O_BLOCK},
    { STR_AND_LEN("remove_ignore_ip"),  PSC_V_REM_IP,   PSC_O_IGNORE},
    { STR_AND_LEN("remove_allow_ip"),   PSC_V_REM_IP,   PSC_O_ALLOW},
    { STR_AND_LEN("reset_ignores"),     PSC_V_RESET,    PSC_O_IGNORE},
    { 0, 0, 0, 0 }
};

static char *getnames[N_IPLIST] = {
    "blocks",
    "ignores",
    "alloweds"
};

static int find_command(int name_len, const char *name_str, int *verb, int *object) {
    struct pubsub_commands *p;

    for (p=pubsub_commands; p->psc_commandstr; p++) {
        //
        // We do a strncmp, potential issue with commands that are initial
        // substring of other commands
        // Solved by adding length of string with clever macro
        // Should be done cleaner some day TODO

        if (name_len == p->psc_commandlen &&
                        strncmp(name_str, p->psc_commandstr, name_len)==0) {
            // Match
            *verb = p->psc_verb;
            *object = p->psc_object;
            return 1;
        }
    }
    return 0;
}


#define MAXNAMELEN      80      // Maximum identifier length; TODO */


void handle_json_command_detail(int verb, int object,
                           const char* json_str,
                           jsmntok_t* tokens,
                           int argument_token_i) {
    int node_id_arg = 0;
    ip_t ip_arg;
    char str_arg[MAXNAMELEN+1];
    // in a few cases, we need to update the node cache
    node_t* node;

    //
    // First some common argument handling
    //

    switch(verb) {
    case PSC_V_ADD:
    case PSC_V_REM:
        // Add names is different
        if (object == PSC_O_NAME)
            break;
        if (!json_parse_int_arg(&node_id_arg, json_str, tokens, argument_token_i)) {
            spin_log(LOG_ERR, "Cannot parse node_id\n");
            return;
        }
        spin_log(LOG_DEBUG, "Spin verb %d, object %d, node-id %d\n", verb, object, node_id_arg);
        break;
    case PSC_V_REM_IP:
        if (!json_parse_ip_arg(&ip_arg, json_str, tokens, argument_token_i)) {
            spin_log(LOG_ERR, "Cannot parse ip-addr\n");
            return;
        }
        spin_log(LOG_DEBUG, "Spin verb %d, object %d, ip XX\n", verb, object);
        break;
    case PSC_V_RESET:
        if (object != PSC_O_IGNORE) {
            spin_log(LOG_ERR, "Reset of non-ignore\n");
            return;
        }
        handle_command_reset_ignores();
        return;
    }

    //
    // Now handle objects
    //

    switch(object) {
    case PSC_O_NAME:
        switch(verb) {
        case PSC_V_GET:
            // TODO
            // handle_command_get_names();
            break;
        case PSC_V_ADD:
            if (!json_parse_node_id_name_arg(&node_id_arg, str_arg, MAXNAMELEN, json_str, tokens, argument_token_i)) {
                spin_log(LOG_ERR, "Cannot parse node_id\n");
                return;
            }
            handle_command_add_name(node_id_arg, str_arg);
            break;
        }
        break;  //NAME

    case PSC_O_BLOCK:
    case PSC_O_IGNORE:
    case PSC_O_ALLOW:
        switch(verb) {
        case PSC_V_GET:
            break;
        case PSC_V_ADD:
        case PSC_V_REM:
            handle_list_membership(object, verb, node_id_arg);
            break;
        case PSC_V_REM_IP:
            handle_command_remove_ip_from_list(object, &ip_arg);
            node = node_cache_find_by_ip(node_cache, sizeof(ip_t), &ip_arg);
            if (node) {
                node->is_onlist[object] = 0;
            }
            break;
        }
        handle_command_get_iplist(object, getnames[object]);
        break;  //BLOCK IGNORE and ALLOW
    }
}

void handle_json_command(const char* data) {
    jsmn_parser p;
    // todo: alloc these upon global init, realloc when necessary?
    const size_t tok_count = 10;
    jsmntok_t tokens[tok_count];
    int result;
    int verb, object;
    int datalen;
    STAT_COUNTER(ctr, message-bytes-in, STAT_TOTAL);

    datalen = strlen(data);
    STAT_VALUE(ctr, datalen);
    jsmn_init(&p);
    result = jsmn_parse(&p, data, datalen, tokens, 10);
    if (result < 0) {
        spin_log(LOG_ERR, "Error: unable to parse json data: %d\n", result);
        return;
    }
    // token should be object, first child should be "command":
    if (tokens[0].type != JSMN_OBJECT) {
        spin_log(LOG_ERR, "Error: unknown json data\n");
        return;
    }
    // token 1 should be "command",
    // token 2 should be the command name (e.g. "get_ignores")
    // token 3 should be "arguments",
    // token 4 should be an object with the arguments (possibly empty)
    if (tokens[1].type != JSMN_STRING || strncmp(data+tokens[1].start, "command", 7) != 0) {
        spin_log(LOG_ERR, "Error: json data not command\n");
        return;
    }
    if (tokens[3].type != JSMN_STRING || strncmp(data+tokens[3].start, "argument", 8) != 0) {
        spin_log(LOG_ERR, "Error: json data does not contain argument field\n");
        return;
    }
    if (find_command(tokens[2].end - tokens[2].start, data+tokens[2].start,
                        &verb, &object)) {
        handle_json_command_detail(verb, object, data, tokens, 4);
        return;
    }
    spin_log(LOG_ERR, "Error: json command not understood\n");
}


// Hook from Mosquitto code called with incoming messages

void do_mosq_message(struct mosquitto* mosq, void* user_data, const struct mosquitto_message* msg) {

    if (strcmp(msg->topic, mqtt_channel_commands) == 0) {
        handle_json_command(msg->payload);
    }

    // TODO, what if other channel?
}

void connect_mosquitto(const char* host, int port) {
    const char* client_name = "asdf";
    int result;

    if (mosq != NULL) {
        mosquitto_disconnect(mosq);
        mosquitto_destroy(mosq);
    }

    mosq = mosquitto_new(client_name, 1, NULL);
    spin_log(LOG_INFO, "Connecting to mqtt server on %s:%d\n", host, port);
    result = mosquitto_connect(mosq, host, port, mosquitto_keepalive_time);
    if (result != 0) {
        spin_log(LOG_ERR, "Error connecting to mqtt server on %s:%d, %s\n", host, port, mosquitto_strerror(result));
        exit(1);
    }
    spin_log(LOG_INFO, "Connected to mqtt server on %s:%d with keepalive value %d\n", host, port, mosquitto_keepalive_time);
    result = mosquitto_subscribe(mosq, NULL, mqtt_channel_commands, 0);
    if (result != 0) {
        spin_log(LOG_ERR, "Error subscribing to topic %s: %s\n", mqtt_channel_commands, mosquitto_strerror(result));
    }

    mosquitto_message_callback_set(mosq, do_mosq_message);

}

void wf_mosquitto(void* arg, int data, int timeout) {
    int result;

    STAT_COUNTER(ctr_data, wf-data, STAT_TOTAL);
    STAT_COUNTER(ctr_timeout, wf-timeout, STAT_TOTAL);

    if (data) {
        STAT_VALUE(ctr_data, 1);
        result = mosquitto_loop_read(mosq, 1);
        if (result != 0) {
            spin_log(LOG_ERR, "Error calling mosquitto_loop_read(): %s\n", mosquitto_strerror(result));
            exit(1);
        }
    }
    if (timeout) {
        STAT_VALUE(ctr_timeout, 1);
        result = mosquitto_loop_write(mosq, 1);
        if (result != 0) {
            spin_log(LOG_ERR, "Error calling mosquitto_loop_write(): %s\n", mosquitto_strerror(result));
            exit(1);
        }
        result = mosquitto_loop_misc(mosq);
        if (result != 0) {
            spin_log(LOG_ERR, "Error calling mosquitto_loop_misc(): %s\n", mosquitto_strerror(result));
            exit(1);
        }
    }
}

void init_mosquitto(const char* host, int port) {
    int object;

    mosquitto_lib_init();

    mqtt_channel_traffic = spinconfig_pubsub_channel_traffic();
    mqtt_channel_commands = spinconfig_pubsub_channel_commands();
    mosquitto_keepalive_time = spinconfig_pubsub_timeout();
    spin_log(LOG_DEBUG, "Mosquitto traffic on %s, commands on %s, timeout %d\n", mqtt_channel_traffic, mqtt_channel_commands, mosquitto_keepalive_time);

    connect_mosquitto(host, port);

    mainloop_register("mosq", &wf_mosquitto, (void *) 0,
            mosquitto_socket(mosq), mosquitto_keepalive_time*1000/2);
    mosquitto_socket(mosq);

    send_command_restart();
    for (object = 0; object < N_IPLIST; object++) {
        handle_command_get_iplist(object, getnames[object]);
    }

    //handle_command_get_iplist(IPLIST_BLOCK, "blocks");
    //handle_command_get_iplist(IPLIST_IGNORE, "ignores");
    //handle_command_get_iplist(IPLIST_ALLOW, "alloweds");
}

void finish_mosquitto() {

    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
}
