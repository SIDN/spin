/*
 * This module of the SPIN web api handles traffic captures;
 * - manage tcpdump sessions (perhaps change that to direct pcap soonish)
 * - handle 'direct' (old style) tcpdump requests for clients with MHD
 * - TODO: handle mqtt (new style) tcpdump requests
 */

#include <microhttpd.h>
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

int tc_get_bytes_sent_for(const char* device_mac) {
    capture_process_t* cp = get_capture_running_for(device_mac);
    if (cp != NULL) {
        return cp->byte_count;
    }
    return -1;
}


void
tc_stop_capture_for(const char* device_mac) {
    capture_process_t* cp = get_capture_running_for(device_mac);
    if (cp != NULL) {
        cp->stop = 1;
    }
}

void tc_stop_all_captures() {
    capture_process_t* cp = cp_global;
    while (cp != NULL) {
        cp->stop = 1;
        cp = cp->next;
    }
}

/* Creates and sets defaults for a capture process data structure */
capture_process_t* capture_process_create(const char* device_mac) {
    capture_process_t* ld = malloc(sizeof(capture_process_t));
    ld->byte_count = 0;
    ld->process = NULL;
    ld->stop = 0;
    ld->stopped = 0;
    ld->prev = NULL;
    ld->next = NULL;
    ld->mac = strdup(device_mac);
    return ld;
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
