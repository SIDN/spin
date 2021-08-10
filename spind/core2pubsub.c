#include <mosquitto.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/stat.h>

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

// We always need a pid file, even when hard-set to empty
const char*
get_mosquitto_pid_file() {
    char* pid_file = spinconfig_pubsub_run_pid_file();
    if (pid_file != NULL && strlen(pid_file) > 0) {
        return pid_file;
    } else {
        return "/var/run/spind_mosquitto.pid";
    }
}

int
remove_mosquitto_pid_file() {
    const char* pid_file = get_mosquitto_pid_file();
    struct stat pf_st;

    if (!stat(pid_file, &pf_st)) {
        if (S_ISDIR(pf_st.st_mode)) {
            return rmdir(pid_file);
        } else {
            return unlink(pid_file);
        }
    } else {
        return 0;
    }
}

int
read_mosquitto_pid_file(int wait_time) {
    int i;
    const char* pid_file = get_mosquitto_pid_file();
    struct stat pf_st;
    FILE* in;
    int result;

    for (i = 0; i < wait_time; i++) {
        spin_log(LOG_DEBUG, "waiting for mosquitto pid file\n");
        if (!stat(pid_file, &pf_st)) {
            spin_log(LOG_DEBUG, "mosquitto pid file found\n");
            in = fopen(pid_file, "r");
            if (in != NULL) {
                if (fscanf(in, "%d", &result) == 1) {
                    fclose(in);
                    spin_log(LOG_DEBUG, "read mosquitto pid %d\n", result);
                    return result;
                }
                fclose(in);
            }
        }

        sleep(1);
    }
    return -1;
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

int connect_mosquitto(const char* host, int port) {
    const char* client_name = "SPIN Daemon";
    int result;
    int i;

    if (mosq != NULL) {
        mosquitto_disconnect(mosq);
        mosquitto_destroy(mosq);
    }

    mosq = mosquitto_new(client_name, 1, NULL);
    if (mosq == NULL) {
        spin_log(LOG_ERR, "Error creating mqtt client instance: %s\n", strerror(errno));
        return 1;
    }

    // try a few times in case it is still starting up
    for (i=0; i<60; i++) {
        spin_log(LOG_INFO, "Connecting to mqtt server on %s:%d\n", host, port);
        result = mosquitto_connect(mosq, host, port, mosquitto_keepalive_time);
        if (result == 0) {
            spin_log(LOG_INFO, "Connected to mqtt server on %s:%d with keepalive value %d\n", host, port, mosquitto_keepalive_time);

            result = mosquitto_subscribe(mosq, NULL, mqtt_channel_jsonrpc_q, 0);
            if (result != 0) {
                spin_log(LOG_ERR, "Error subscribing to topic %s: %s\n", mqtt_channel_jsonrpc_q, mosquitto_strerror(result));
                return 1;
            }
            mosquitto_message_callback_set(mosq, do_mosq_message);
            return 0;
        }
        sleep(1);
    }
    spin_log(LOG_ERR, "Error connecting to mqtt server on %s:%d, %s\n", host, port, mosquitto_strerror(result));
    return 1;
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
    mosq_conf = fopen(mosq_conf_filename, "w");
    char* tls_cert_file = NULL;
    char* tls_key_file = NULL;

    char* dup;
    char* p;

    if (mosq_conf == NULL) {
        spin_log(LOG_ERR, "fopen %s: %s\n", mosq_conf_filename, strerror(errno));
        return 1;
    }

    fprintf(mosq_conf, "pid_file %s\n", get_mosquitto_pid_file());

    // Enable per-listener settings
    fprintf(mosq_conf, "per_listener_settings true\n");

    // Always listen on localhost 1883
    //fprintf(mosq_conf, "port 1883 127.0.0.1\n");
    fprintf(mosq_conf, "port %d %s\n", pubsub_port, pubsub_host);
    fprintf(mosq_conf, "allow_anonymous true\n");

    // Configure the websockets listener(s).
    // It MUST use the same configuration as spinweb for a number of
    // settings (such as https; a ws-url will not be opened from an https
    // page, due to browser security policies).
    // Using the same cert and key is not strictly necessary, but does
    // save a lot of maintenance work regarding maintenance of separate
    // x509 certificates.

    tls_cert_file = spinconfig_spinweb_tls_certificate_file();
    tls_key_file = spinconfig_spinweb_tls_key_file();

    // Create a websockets listener for each interface spinweb listens on
    // (this will generally be the location(s) the user will access it
    // anyway
    // Loop over the list of interfaces (dup), tokenize into p
    dup = strdup(spinconfig_spinweb_interfaces());
    spin_log(LOG_DEBUG, "[XX] spinweb_interfaces: '%s'\n", dup);
    p = strtok (dup,",");
    while (p != NULL) {
        while (*p == ' ') {
            p++;
        }
        spin_log(LOG_DEBUG, "[XX] adding websockets listener: '%s' port %d\n", p, pubsub_websocket_port);
        fprintf(mosq_conf, "listener %d %s\n", pubsub_websocket_port, p);
        // Mosquitto will try to default to some IPv6 addresses if we give
        // an IPv4 address, which will fail if we do this multiple times,
        // so we need to explicitely tell it what IP protocol we are using
        if (is_ipv4_address(p)) {
            fprintf(mosq_conf, "socket_domain ipv4\n");
        } else if (is_ipv6_address(p)) {
            fprintf(mosq_conf, "socket_domain ipv6\n");
        }

        fprintf(mosq_conf, "protocol websockets\n");

        char* password_file = spinconfig_pubsub_run_password_file();
        if (password_file != NULL && strlen(password_file) > 0) {
            fprintf(mosq_conf, "allow_anonymous false\n");
            fprintf(mosq_conf, "password_file %s\n", password_file);
        } else {
            fprintf(mosq_conf, "allow_anonymous true\n");
        }

        if (tls_cert_file != NULL && strlen(tls_cert_file) > 0) {
            fprintf(mosq_conf, "certfile %s\n", tls_cert_file);
        }
        if (tls_key_file != NULL && strlen(tls_key_file) > 0) {
            fprintf(mosq_conf, "keyfile %s\n", tls_key_file);
        }

        p = strtok (NULL, ",");
    }
    free(dup);

    fclose(mosq_conf);

    return 0;
}


int
mosquitto_start_server(const char* host, int port, const char* websocket_host, int websocket_port) {
    if (mosquitto_create_config_file(host, port, websocket_host, websocket_port) != 0) {
        spin_log(LOG_ERR, "Error creating temporary configuration file for mosquitto\n");
        return 1;
    }
    if (remove_mosquitto_pid_file() != 0) {
        spin_log(LOG_ERR, "Error removing existing pid file for mosquitto: %s\n", strerror(errno));
        return 1;
    }
    // Run mosquitto in daemonized mode, so we don't need to deal with
    // fork and file descriptor issues
    //spin_log(LOG_INFO, "[XX] calling fork()\n");
    //int pid = fork();
    //if(pid == 0) {
        //int result;
        char commandline[256];
        spin_log(LOG_INFO, "[XX] at child, closing fds\n");
        //fclose(stdin);
        //fclose(stdout);
        //fclose(stderr);
        signal(SIGCHLD,SIG_IGN);
        snprintf(commandline, 255, "mosquitto -d -c %s", mosq_conf_filename);
        if (system(commandline) != 0) {
            spin_log(LOG_ERR, "Error starting mosquitto\n");
            return 1;
        }
        //result = system(commandline);
        //if (result != 0) {
        //    spin_log(LOG_ERR, "Error starting mosquitto\n");
        //}
        //exit(result);
        
        /*
        fprintf(stderr, "[XX] i am child\n");
        fclose(stdin);
        fclose(stdout);
        fclose(stderr);
        char** argv = malloc(5*sizeof(char*));

        argv[0] = strdup("mosquitto");
        argv[1] = strdup("-d");
        argv[2] = strdup("-c");
        argv[3] = strdup(mosq_conf_filename);
        argv[4] = NULL;
        execvp("mosquitto", argv);
        exit(0);
        */
    //} else {
        spin_log(LOG_INFO, "[XX] at parent\n");
        // Prevent the child process from going defunct when it exits
        //signal(SIGCHLD,SIG_IGN);
        //wait(NULL);
        mosq_pid = read_mosquitto_pid_file(5);
        if (mosq_pid < 1) {
            spin_log(LOG_ERR, "Unable to read mosquitto pid file, mosquitto will probably keep running when spin stops\n");
        } else {
            spin_log(LOG_INFO, "Mosquitto server started with pid %d\n", mosq_pid);
        }

        // Remember child PID so that we can stop the mosquitto server
        // when spind itself stops
        //mosq_pid = pid;
        //sleep(5);
        return 0;
    //}
}

int old_mosquitto_start_server(const char* host, int port, const char* websocket_host, int websocket_port) {
    char commandline[256];
    int result;
    if (mosquitto_create_config_file(host, port, websocket_host, websocket_port) != 0) {
        spin_log(LOG_ERR, "Error creating temporary configuration file for mosquitto\n");
        return 1;
    }
    fflush(stdout);
    int pid = fork();
    result = 1234;
    if(pid == 0) {
        // mosquitto only writes a pid file when in daemon mode
        // but if there is no pid file configure we do NOT want daemon
        // mode (we want to send TERM to this fork in that case)
        char* pid_file = spinconfig_pubsub_run_pid_file();
        if (pid_file && strlen(pid_file) > 0) {
            signal(SIGCHLD,SIG_IGN);
            snprintf(commandline, 255, "mosquitto -d -c %s", mosq_conf_filename);
        } else {
            snprintf(commandline, 255, "mosquitto -c %s", mosq_conf_filename);
        }
        result = system(commandline);
        exit(result);
    }
    // Prevent the child process from going defunct when it exits
    signal(SIGCHLD,SIG_IGN);

    // As a fallback case if spin is not configured to store the mosquitto
    // pid in a file, store the fork() pid and use that to stop mosquitto
    // note that this should be a fallback only, as it does not always
    // work when spin itself receives a SIGTERM
    mosq_pid = pid;
    spin_log(LOG_INFO, "Mosquitto server started with pid %d\n", mosq_pid);

    //result = 1;
    fflush(stdout);
    sleep(2);
    // TODO Check if it is running?
    return 0;
}


int read_pid (char *pidfile)
{
  FILE *f;
  int pid;

  if (!(f=fopen(pidfile,"r")))
    return 0;
  if (fscanf(f,"%d", &pid) != 1)
    return 0;
  fclose(f);
  return pid;
}

void mosquitto_stop_server() {
    // if we have a pid file for mosquitto, use that to kill it
    // Otherwise, use the fallback option (our fork() sibling)
    char* pid_file = spinconfig_pubsub_run_pid_file();
    if (pid_file != NULL && strlen(pid_file) > 0) {
        spin_log(LOG_INFO, "Reading pid from file %s\n", pid_file);
        int file_pid = read_pid(pid_file);
        if (file_pid != 0) {
            mosq_pid = file_pid;
        }
    }
    spin_log(LOG_INFO, "Stopping mosquitto server with pid %d\n", mosq_pid);
    kill(mosq_pid, SIGTERM);
    sleep(2);
    kill(mosq_pid, SIGKILL);
    spin_log(LOG_INFO, "Mosquitto server has been stopped\n");

    // Remove the temporary file, doublecheck
    // /tmp/spin_mosq_conf_
    if (strncmp("/tmp/spin_mosq_conf_", mosq_conf_filename, 20) == 0) {
        remove(mosq_conf_filename);
    }
}

int init_mosquitto(int start_own_instance, const char* host, int port, const char* websocket_host, int websocket_port) {
    int object;

    if (start_own_instance) {
        spin_log(LOG_INFO, "Starting mosquitto server\n");
        if (mosquitto_start_server(host, port, websocket_host, websocket_port) != 0) {
            spin_log(LOG_ERR, "Error starting mosquitto server, aborting\n");
            return 1;
        }
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
    return 0;
}

void finish_mosquitto(int started_own_instance) {

    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();

    if (started_own_instance) {
        mosquitto_stop_server();
    }
}
