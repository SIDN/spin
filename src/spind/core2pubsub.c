#include <stdlib.h>
#include <mosquitto.h>

#include "spindata.h"

#include "node_cache.h"
#include "util.h"
#include "mainloop.h"
#include "spinconfig.h"
#include "spin_log.h"
#include "spin_list.h"
#include "handle_command.h"
#include "statistics.h"

#include "rpc_json.h"

static char *mqtt_channel_traffic;
static char *mqtt_channel_commands;
static char mqtt_channel_jsonrpc_q[] = "SPIN/jsonrpc/q";
static char mqtt_channel_jsonrpc_a[] = "SPIN/jsonrpc/a";
static int mosquitto_keepalive_time;

struct mosquitto* mosq;
extern node_cache_t* node_cache;

STAT_MODULE(pubsub)

/*
 * Code that pushes gathered results back to Spin traffic channel for
 * UI code(and others?) to do their work with
 */

void
pubsub_publish(char *channel, int payloadlen, const void* payload, int retain) {

    /*
     * There is a result from command, but for now ignored
     */
    mosquitto_publish(mosq, NULL, channel, payloadlen, payload, 0, retain);
}

void core2pubsub_publish_chan(char *channel, spin_data sd, int retain) {
    STAT_COUNTER(ctr, chan-publish, STAT_TOTAL);
    char *message;
    int message_len;

    if (channel == NULL) {
        // default
        channel = mqtt_channel_traffic;
    }

    if (sd != NULL) {
        message = spin_data_serialize(sd);
        message_len = strlen(message);
        STAT_VALUE(ctr, message_len);
        pubsub_publish(channel, message_len, message, retain);
        spin_data_ser_delete(message);
    } else {
        // Empty message to retain
        pubsub_publish(channel, 0, "", retain);
    }
}

/* End push back code */

/* (Re)start command Mosquitto server only */

void send_command_restart() {
    spin_data cmd_sd;

    cmd_sd = spin_data_create_mqtt_command("serverRestart", NULL, NULL);
    core2pubsub_publish_chan(NULL, cmd_sd, 0);

    spin_data_delete(cmd_sd);
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

static struct pubsub_commands {
    char *      psc_commandstr;         // String of command
    int         psc_verb;               // Verb
    int         psc_object;             // Object
} pubsub_commands[] = {
    { "get_blocks",        PSC_V_GET,      PSC_O_BLOCK},
    { "get_ignores",       PSC_V_GET,      PSC_O_IGNORE},
    { "get_alloweds",      PSC_V_GET,      PSC_O_ALLOW},
    { "get_names",         PSC_V_GET,      PSC_O_NAME },
    { "add_block_node",    PSC_V_ADD,      PSC_O_BLOCK},
    { "add_ignore_node",   PSC_V_ADD,      PSC_O_IGNORE},
    { "add_allow_node",    PSC_V_ADD,      PSC_O_ALLOW},
    { "add_name",          PSC_V_ADD,      PSC_O_NAME},
    { "remove_block_node", PSC_V_REM,      PSC_O_BLOCK},
    { "remove_ignore_node",PSC_V_REM,      PSC_O_IGNORE},
    { "remove_allow_node", PSC_V_REM,      PSC_O_ALLOW},
    { "remove_block_ip",   PSC_V_REM_IP,   PSC_O_BLOCK},
    { "remove_ignore_ip",  PSC_V_REM_IP,   PSC_O_IGNORE},
    { "remove_allow_ip",   PSC_V_REM_IP,   PSC_O_ALLOW},
    { "reset_ignores",     PSC_V_RESET,    PSC_O_IGNORE},
    { 0, 0, 0 }
};

static char *getnames[N_IPLIST] = {
    "block",
    "ignore",
    "allow"
};

static int find_command(const char *name_str, int *verb, int *object) {
    struct pubsub_commands *p;

    for (p=pubsub_commands; p->psc_commandstr; p++) {
        if (strcmp(name_str, p->psc_commandstr)==0) {
            // Match
            *verb = p->psc_verb;
            *object = p->psc_object;
            return 1;
        }
    }
    return 0;
}

int getint_cJSONobj(cJSON *cjarg, char *fieldname) {
    cJSON *f_json;

    f_json = cJSON_GetObjectItemCaseSensitive(cjarg, fieldname);
    if (!cJSON_IsNumber(f_json)) {
        return 0;
    }
    return f_json->valueint;
}

char* getstr_cJSONobj(cJSON *cjarg, char *fieldname) {
    cJSON *f_json;

    f_json = cJSON_GetObjectItemCaseSensitive(cjarg, fieldname);
    if (!cJSON_IsString(f_json)) {
        return NULL;
    }
    return f_json->valuestring;
}

void handle_json_command_detail(int verb, int object, cJSON *argument_json) {
    int node_id_arg = 0;
    ip_t ip_arg;
    char *str_arg;
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
        if (!cJSON_IsNumber(argument_json)) {
            spin_log(LOG_ERR, "Cannot parse node_id\n");
            return;
        }
        node_id_arg = argument_json->valueint;
        break;
    case PSC_V_REM_IP:
        if (!cJSON_IsString(argument_json) || !spin_pton(&ip_arg, argument_json->valuestring)) {
            spin_log(LOG_ERR, "Cannot parse ip-addr\n");
            return;
        }
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
            node_id_arg = getint_cJSONobj(argument_json, "node-id");
            str_arg = getstr_cJSONobj(argument_json, "name");
            if (node_id_arg != 0 && str_arg != NULL) {
                handle_command_add_name(node_id_arg, str_arg);
            }
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

void
handle_json_command(char *data) {
    cJSON *command_json;
    cJSON *method_json;
    cJSON *argument_json;
    char *method;
    int verb, object;

    command_json = cJSON_Parse(data);
    if (command_json == NULL) {
        spin_log(LOG_ERR, "Unable to parse command\n");
        goto end;
    }

    method_json = cJSON_GetObjectItemCaseSensitive (command_json, "command");
    if (!cJSON_IsString(method_json)) {
        spin_log(LOG_ERR, "No command found\n");
        goto end;
    }
    method = method_json->valuestring;

    argument_json = cJSON_GetObjectItemCaseSensitive (command_json, "argument");
    if (argument_json == NULL) {
        spin_log(LOG_ERR, "No argument found\n");
        goto end;
    }

    if (find_command(method, &verb, &object)) {
        handle_json_command_detail(verb, object, argument_json);
    }

end:
    cJSON_Delete(command_json);
}


// Hook from Mosquitto code called with incoming messages

void do_mosq_message(struct mosquitto* mosq, void* user_data, const struct mosquitto_message* msg) {
    char *result;

    if (strcmp(msg->topic, mqtt_channel_commands) == 0) {
        handle_json_command(msg->payload);
        return;
    }
    if (strcmp(msg->topic, mqtt_channel_jsonrpc_q) == 0) {
        spin_log(LOG_DEBUG, "Rpc channel: %s\n", msg->payload);
        result = call_string_jsonrpc(msg->payload);
        if (result != NULL) {
            pubsub_publish(mqtt_channel_jsonrpc_a, strlen(result), result, 0);
            spin_data_ser_delete(result);
        }
        return;
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
    if (mosq == NULL) {
        spin_log(LOG_ERR, "Error creating mqtt client instance: %s\n", strerror(errno));
        exit(1);
    }
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
        exit(1);
    }

    result = mosquitto_subscribe(mosq, NULL, mqtt_channel_jsonrpc_q, 0);
    if (result != 0) {
        spin_log(LOG_ERR, "Error subscribing to topic %s: %s\n", mqtt_channel_jsonrpc_q, mosquitto_strerror(result));
        exit(1);
    }

    mosquitto_message_callback_set(mosq, do_mosq_message);

}

void wf_mosquitto(void* arg, int data, int timeout) {
    STAT_COUNTER(ctr_data, wf-data, STAT_TOTAL);
    STAT_COUNTER(ctr_timeout, wf-timeout, STAT_TOTAL);

    if (data) {
        STAT_VALUE(ctr_data, 1);
        mosquitto_loop_read(mosq, 1);
    }
    if (timeout) {
        STAT_VALUE(ctr_timeout, 1);
        mosquitto_loop_write(mosq, 1);
        mosquitto_loop_misc(mosq);
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
}

void finish_mosquitto() {

    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
}
