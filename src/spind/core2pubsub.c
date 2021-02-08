#include <mosquitto.h>

#include "ipl.h"
#include "mainloop.h"
#include "rpc_json.h"
#include "spin_config.h"
#include "spin_log.h"
#include "statistics.h"

static char *mqtt_channel_traffic;
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

static char *getnames[N_IPLIST] = {
    "block",
    "ignore",
    "allow"
};

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

// Hook from Mosquitto code called with incoming messages

void do_mosq_message(struct mosquitto* mosq, void* user_data, const struct mosquitto_message* msg) {
    char *result;

    if (strcmp(msg->topic, mqtt_channel_jsonrpc_q) == 0) {
        spin_log(LOG_DEBUG, "Rpc channel: %s\n", (char *)msg->payload);
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

void broadcast_iplist(int iplist, const char* list_name) {
    spin_data ipt_sd, cmd_sd;

    ipt_sd = spin_data_ipar(get_spin_iplist(iplist)->li_tree);
    cmd_sd = spin_data_create_mqtt_command(list_name, NULL, ipt_sd);

    core2pubsub_publish_chan(NULL, cmd_sd, 0);

    spin_data_delete(cmd_sd);
}

#include <stdio.h>
#include <signal.h>
#define MOSQ_CONF_TEMPLATE "/tmp/spin_mosq_conf_XXXXXX"
static char mosq_conf_filename[27];
static int mosq_pid = 0;

int
mosquitto_create_config_file(const char* pubsub_host, int pubsub_port, const char* pubsub_websocket_host, int pubsub_websocket_port) {
    FILE* mosq_conf;
    sprintf(mosq_conf_filename, MOSQ_CONF_TEMPLATE);
    if (mkstemp(mosq_conf_filename) == -1) {
        spin_log(LOG_ERR, "mkstemp %s: %s\n", mosq_conf_filename, strerror(errno));
        return 1;
    }
    printf("Tempname #1: %s\n", mosq_conf_filename);
    mosq_conf = fopen(mosq_conf_filename, "w");
    char* tls_cert_file = NULL;
    char* tls_key_file = NULL;

    if (mosq_conf == NULL) {
        spin_log(LOG_ERR, "fopen %s: %s\n", mosq_conf_filename, strerror(errno));
        return 1;
    }
    // Always listen on localhost 1883, no extra settings necessary
    fprintf(mosq_conf, "port 1883 127.0.0.1\n");
    fprintf(mosq_conf, "port %d %s\n", pubsub_port, pubsub_host);

    // Configure the websockets listener.
    // It MUST use the same configuration as spinweb for a number of
    // settings (such as https; a ws-url will not be opened from an https
    // page, due to browser security policies).
    // Using the same cert and key is not strictly necessary, but does
    // save a lot of maintenance work regarding maintenance of separate
    // x509 certificates.
    fprintf(mosq_conf, "listener %d %s\n", pubsub_websocket_port, pubsub_websocket_host);
    fprintf(mosq_conf, "protocol websockets\n");
    fprintf(mosq_conf, "allow_anonymous true\n");

    tls_cert_file = spinconfig_spinweb_tls_certificate_file();
    tls_key_file = spinconfig_spinweb_tls_key_file();
    if (tls_cert_file != NULL && strlen(tls_cert_file) > 0) {
        fprintf(mosq_conf, "certfile %s\n", tls_cert_file);
    }
    if (tls_key_file != NULL && strlen(tls_key_file) > 0) {
        fprintf(mosq_conf, "keyfile %s\n", tls_key_file);
    }

    fclose(mosq_conf);

    return 0;
}

int mosquitto_start_server(const char* host, int port, const char* websocket_host, int websocket_port) {
    char commandline[256];
    int result;
    if (mosquitto_create_config_file(host, port, websocket_host, websocket_port) != 0) {
        fprintf(stderr, "Error creating temporary configuration file for mosquitto\n");
        return 1;
    }
    fflush(stdout);
    // TODO: use posix_spawn?
    int pid = fork();
    result = 1234;
    if(pid == 0) {
        snprintf(commandline, 255, "mosquitto -c %s", mosq_conf_filename);
        result = system(commandline);
        exit(result);
    }
    mosq_pid = pid;

    //result = 1;
    fflush(stdout);
    sleep(2);
    // TODO Check if it is running?
    return 0;
}

void mosquitto_stop_server() {
    spin_log(LOG_INFO, "Stopping mosquitto server");
    kill(mosq_pid, SIGTERM);
    sleep(2);
    kill(mosq_pid, SIGKILL);
    spin_log(LOG_INFO, "Mosquitto server has been stopped");

    // Remove the temporary file, doublecheck
    // /tmp/spin_mosq_conf_
    if (strncmp("/tmp/spin_mosq_conf_", mosq_conf_filename, 20) == 0) {
        remove(mosq_conf_filename);
    }
}

void init_mosquitto(int start_own_instance, const char* host, int port, const char* websocket_host, int websocket_port) {
    int object;

    if (start_own_instance) {
        spin_log(LOG_INFO, "Starting mosquitto server");
        mosquitto_start_server(host, port, websocket_host, websocket_port);
    }

    mosquitto_lib_init();

    mqtt_channel_traffic = spinconfig_pubsub_channel_traffic();
    mosquitto_keepalive_time = spinconfig_pubsub_timeout();
    spin_log(LOG_DEBUG, "Mosquitto traffic on %s, timeout %d\n", mqtt_channel_traffic, mosquitto_keepalive_time);

    connect_mosquitto(host, port);

    mainloop_register("mosq", &wf_mosquitto, (void *) 0,
            mosquitto_socket(mosq), mosquitto_keepalive_time*1000/2, 1);
    mosquitto_socket(mosq);

    send_command_restart();
    for (object = 0; object < N_IPLIST; object++) {
        broadcast_iplist(object, getnames[object]);
    }
}

void finish_mosquitto(int started_own_instance) {

    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();

    if (started_own_instance) {
        mosquitto_stop_server();
    }
}
