/*
 * This module of the SPIN web api handles traffic captures;
 * - manage tcpdump sessions (perhaps change that to direct pcap soonish)
 * - handle 'direct' (old style) tcpdump requests for clients with MHD
 * - TODO: handle mqtt (new style) tcpdump requests
 */

#include <microhttpd.h>
#include <mosquitto.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

typedef struct capture_process {
    // Set to 1 to stop at next iteration
    int stop;
    // Is set to 1 when process has stopped
    int stopped;
    // Number of bytes captured
    int byte_count;
    // The actual process
    FILE* process;
    // The MAC address that is being captured
    char* mac;

    // In the case of MQTT (new-style) captures, we need to keep track
    // of some more information.
    struct mosquitto* mqtt_client;
    char* mqtt_channel;

    /* Make it a doubly linked list for easier maintenance */
    struct capture_process* prev;
    struct capture_process* next;
} capture_process_t;

capture_process_t* cp_global = NULL;

int
tc_captures_running() {
    capture_process_t* cp = cp_global;
    int i = 0;
    while (cp != NULL) {
        i++;
        cp = cp->next;
    }
    return i;
}

/*
 * returns NULL if there is no capture running for this mac
 */
capture_process_t*
get_capture_running_for(const char* device_mac) {
    capture_process_t* cp = cp_global;
    while (cp != NULL) {
        if (strcmp(device_mac, cp->mac) == 0) {
            return cp;
        }
        cp = cp->next;
    }
    return NULL;
}

/**
 * Returns 1 if there is a capture running for the given mac address, 0 otherwise
 */
int
tc_capture_running_for(const char* device_mac) {
    return get_capture_running_for(device_mac) != NULL;
}

/**
 * Returns the number of bytes captured so far for the given mac address
 */
int tc_get_bytes_sent_for(const char* device_mac) {
    capture_process_t* cp = get_capture_running_for(device_mac);
    if (cp != NULL) {
        return cp->byte_count;
    }
    return -1;
}

/**
 * Stops capturing packets for the given mac address
 */
void
tc_stop_capture_for(const char* device_mac) {
    capture_process_t* cp = get_capture_running_for(device_mac);
    if (cp != NULL) {
        cp->stop = 1;
    }
}

/**
 * Stop all the packet captures
 */
void tc_stop_all_captures() {
    capture_process_t* cp = cp_global;
    while (cp != NULL) {
        cp->stop = 1;
        cp = cp->next;
    }
}

/* Creates and sets defaults for a capture process data structure */
capture_process_t* capture_process_create(const char* device_mac) {
    capture_process_t* cp = malloc(sizeof(capture_process_t));
    cp->byte_count = 0;
    cp->process = NULL;
    cp->stop = 0;
    cp->stopped = 0;
    cp->prev = NULL;
    cp->next = NULL;
    cp->mac = strdup(device_mac);
    cp->mqtt_client = NULL;
    cp->mqtt_channel = NULL;
    return cp;
}

void capture_process_stop(capture_process_t* cp) {
    if (cp->process != NULL) {
        pclose(cp->process);
        cp->process = NULL;
    }
    cp->stopped = 1;
}

static void capture_process_destroy(void* cp_v) {
    capture_process_t* cp = (capture_process_t*)cp_v;
    capture_process_stop(cp);
    if (cp->process != NULL) {
        pclose(cp->process);
    }
    if (cp->mac != NULL) {
        free(cp->mac);
    }
    if (cp->mqtt_client != NULL) {
        mosquitto_disconnect(cp->mqtt_client);
        mosquitto_destroy(cp->mqtt_client);
    }
    if (cp->mqtt_channel != NULL) {
        free(cp->mqtt_channel);
    }
    free(cp);
}

/* Creates a new capture process data structure and adds it to the global list */
capture_process_t* ct_add_capture_process(const char* device_mac) {
    capture_process_t* new = capture_process_create(device_mac);
    capture_process_t* last;
    if (cp_global == NULL) {
        cp_global = new;
    } else {
        last = cp_global;
        while (last->next != NULL) {
            last = last->next;
        }
        last->next = new;
        new->prev = last;
    }
    return new;
}

/* Destroys a capture process data structure and removes it from the global list */
void ct_remove_capture_process(void* cp_v) {
    capture_process_t* cp = (capture_process_t*) cp_v;
    capture_process_t* next = cp->next;
    capture_process_t* prev = cp->prev;

    if (prev == NULL) {
        cp_global = next;
    } else {
        prev->next = next;
    }

    if (next != NULL) {
        next->prev = prev;
    }
    capture_process_destroy(cp);
}

ssize_t direct_capture_callback(void* ld_v, uint64_t pos, char* buf, size_t max) {
    capture_process_t* ld = (capture_process_t*)ld_v;
    (void)pos;
    (void)buf;
    (void)max;

    //int read;

    if (ld->stopped) {
        printf("[XX] REQUEST FOR STOPPED SHOULD NOT HAPPEN\n");
        printf("[XX] REQUEST FOR STOPPED SHOULD NOT HAPPEN\n");
        printf("[XX] REQUEST FOR STOPPED SHOULD NOT HAPPEN\n");
        printf("[XX] REQUEST FOR STOPPED SHOULD NOT HAPPEN\n");
        printf("[XX] REQUEST FOR STOPPED SHOULD NOT HAPPEN\n");
        printf("[XX] REQUEST FOR STOPPED SHOULD NOT HAPPEN\n");
        return MHD_CONTENT_READER_END_OF_STREAM;
    } else {
        /* This should go elsewhere */
        printf("[XX] STARTING CAPTURE FOR DEVICE %s\n", ld->mac);
        char cmdline[256];
        snprintf(cmdline, 255, "tcpdump -s 1600 -w - ether host %s", ld->mac);
        printf("[XX] %s", cmdline);
        if (ld->process == NULL) {
            ld->process = popen(cmdline, "r");
        }

        if (ld->process == NULL) {
            // error!
            return MHD_CONTENT_READER_END_WITH_ERROR;
        }
        /* up to here */
        
        // TODO: maybe do one last read if stopped?
        if (ld->stop) {
            capture_process_stop(ld);
            return MHD_CONTENT_READER_END_OF_STREAM;
        } else {
            // We know the first 24 bytes will be available immediately, so
            // start with that
            size_t to_read = max;
            if (ld->byte_count == 0) {
                to_read = 24;
            }
            printf("[XX] capture_callback\n");
            printf("[XX] to read: %ld\n", to_read);
            size_t bread = fread(buf, 1, to_read, ld->process);
            if (bread > 0) {
                printf("[XX] read %lu bytes from process\n", bread);
                ld->byte_count += bread;
                return bread;
            } else {
                printf("[XX] zero bytes read\n");
                return MHD_CONTENT_READER_END_OF_STREAM;
            }
        }
    }
}

/*
 * Convert the data in buf to uppercase hexadecimal, and store it
 * in hexbuf. Hexbuf must have size*2+1 bytes of memory allocated
 */
void
bytes_to_hex(char* hexbuf, char* buf, int size) {
    int i;
    for (i=0; i<size; i++) {
        sprintf(hexbuf+2*i, "%02X", (uint8_t)buf[i]);
    }
}

#define MAX_READ 512
void* process_mqtt_capture(void* cp_v) {
    capture_process_t* cp = (capture_process_t*)cp_v;
    char buf[MAX_READ];
    char hexline[MAX_READ*2+1];
    memset(hexline, 0, MAX_READ*2+1);

    while (cp->stop == 0) {
        size_t bread = fread(buf, 1, MAX_READ, cp->process);
        if (bread > 0) {
            printf("[XX] read %lu bytes from process\n", bread);
            cp->byte_count += bread;
            // TODO send to mqtt
            bytes_to_hex(hexline, buf, bread);
            printf("[XX] first four bytes: %u %u %u %u\n", buf[0], buf[1], buf[2], buf[3]);
            printf("[XX] first four bytes: %02x %02x %02x %02x\n", (uint8_t)buf[0], (uint8_t)buf[1], (uint8_t)buf[2], (uint8_t)buf[3]);
            printf("[XX] PUBLISHING %s to %s\n", hexline, cp->mqtt_channel);
            int mres = mosquitto_publish(cp->mqtt_client, NULL, cp->mqtt_channel, bread*2, hexline, 0, 0);
            printf("[XX] read %lu bytes of data; total: %d\n", bread, cp->byte_count);
            //int mres = mosquitto_loop(cp->mqtt_client, 1, 1);
            printf("[XX] mptr: %p, mosquitto loop result: %d\n", cp->mqtt_client, mres);
        } else {
            printf("[XX] zero bytes read, process seems dead\n");
            break;
        }

        //sleep(1);
    }
    capture_process_stop(cp);
    ct_remove_capture_process(cp);
    printf("[XX] capture stop requested, closing\n");
    return NULL;
}


#define TODO_MOSQ_HOST "127.0.0.1"
#define TODO_MOSQ_PORT 1883
#define TODO_MOSQ_KEEPALIVE 100

#define TCPDUMP_MQTT_CHANNEL "SPIN/capture/"

// TODO not necessary in sync connect, right?
void my_connect_callback(struct mosquitto *mosq, void *obj, int result) {
    printf("[XX] CONNECTED TO MOSQUITTO!\n");
}

/* returns:
 *  0 if the capture was started successfully,
 * -1 if a capture for this mac is already running
 */
int
tc_start_mqtt_capture_for(const char* device_mac) {
    pthread_t capture_thread;
    capture_process_t* cp = get_capture_running_for(device_mac);

    if (cp != NULL) {
        return -1;
    }
    // TODO: move this
    mosquitto_lib_init();

    // start the capture
    cp = ct_add_capture_process(device_mac);

    // determine publication channel based on mac and connect to mqtt
    cp->mqtt_client = mosquitto_new(NULL, true, NULL);

    mosquitto_connect_callback_set(cp->mqtt_client, my_connect_callback);

    cp->mqtt_channel = malloc(strlen(TCPDUMP_MQTT_CHANNEL) + strlen(device_mac) + 1);
    cp->mqtt_channel[0] = '\0';
    strcat(cp->mqtt_channel, TCPDUMP_MQTT_CHANNEL);
    strcat(cp->mqtt_channel, device_mac);

    printf("[XX] connecting to mosquitto with client ptr %p\n", cp->mqtt_client);
    int result = mosquitto_connect(cp->mqtt_client, TODO_MOSQ_HOST, TODO_MOSQ_PORT, TODO_MOSQ_KEEPALIVE);
    if (result != 0) {
        printf("[XX] ERROR CONNECTING TO MOSQUITTO\n");
        ct_remove_capture_process(cp);
        return -2;
    }
    printf("[XX] mosquitto connected: rcode %d\n", result);
    mosquitto_loop(cp->mqtt_client, -1, 1);

    result = mosquitto_loop_start(cp->mqtt_client);
    if (result != 0) {
        printf("[XX] ERROR CONNECTING TO MOSQUITTO\n");
        ct_remove_capture_process(cp);
        return -2;
    }
    printf("[XX] mosquitto loop started\n");


    char cmdline[256];
    snprintf(cmdline, 255, "tcpdump -s 1600 -w - ether host %s", device_mac);
    printf("[XX] %s", cmdline);
    if (cp->process == NULL) {
        cp->process = popen(cmdline, "r");
    }

    if (cp->process == NULL) {
        // error!
        ct_remove_capture_process(cp);
        return -3;
    }

    // make a thread to read and send its data
    if(pthread_create(&capture_thread, NULL, process_mqtt_capture, cp)) {
        fprintf(stderr, "Error creating thread\n");
        ct_remove_capture_process(cp);
        return -4;
    }

    (void)capture_thread;
    return 0;
}


int
tc_answer_direct_capture_request(struct MHD_Connection *connection,
                              const char *url) {

    struct MHD_Response* response;
    int ret;
    const char* device_mac = MHD_lookup_connection_value (connection, MHD_GET_ARGUMENT_KIND, "device");
    capture_process_t* ld = ct_add_capture_process(device_mac);

    response = MHD_create_response_from_callback(-1,
                                                 64,
                                                 direct_capture_callback,
                                                 ld, // callback argument
                                                 ct_remove_capture_process);

    MHD_add_response_header (response, "Content-Type", "application/vnd.tcpdump.pcap");

    ret = MHD_queue_response(connection,
                             MHD_HTTP_OK,
                             response);
    MHD_destroy_response(response);
    return ret;
}
