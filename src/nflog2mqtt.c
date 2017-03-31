/*
 * Listen for NFLOG messages and send them to mqtt
 *
 * (c) 2017 SIDN
 */

#include <arpa/inet.h>
#include <signal.h>

#include <mosquitto.h>

#include "nflog.h"

// global pointer for cleanup on exit
static struct nflog_g_handle* handle;

void cleanup() {
    nflog_cleanup_handle(handle);
    mosquitto_lib_cleanup();
    exit(0);
}

void
hexprint(char* d, size_t s) {
    size_t i;
    printf("00:\t");
    for (i = 0; i < s; i++) {
        if ((i > 0) && (i%10 == 0)) {
            printf("\n%u:\t", (unsigned int)i);
        }
        printf("%02x ", (uint8_t) d[i]);
    }
    printf("\n");
}

#define MSG_SIZE 2048
void
publish_blocked_event(struct mosquitto* mqtt, char* from, char* to, unsigned int timestamp) {
    int result;
    char msg[MSG_SIZE];
    size_t pos = 0;

    pos += snprintf(&msg[pos], MSG_SIZE-pos, "{ ");
    pos += snprintf(&msg[pos], MSG_SIZE-pos, "\"command\": \"blocked\", ");
    pos += snprintf(&msg[pos], MSG_SIZE-pos, "\"argument\": \"\", ");
    pos += snprintf(&msg[pos], MSG_SIZE-pos, "\"result\": { ");
    pos += snprintf(&msg[pos], MSG_SIZE-pos, "\"timestamp\": %u, ", timestamp);
    pos += snprintf(&msg[pos], MSG_SIZE-pos, "\"from\": \"%s\", ", from);
    pos += snprintf(&msg[pos], MSG_SIZE-pos, "\"to\": \"%s\"", to);
    pos += snprintf(&msg[pos], MSG_SIZE-pos, "} }");

    printf("[XX] Sending %u bytes of data:\n%s\n", (unsigned int)pos, msg);
    result = mosquitto_publish(mqtt, NULL, "SPIN/traffic", pos, msg, 0, 0);
    if (result != MOSQ_ERR_SUCCESS) {
        printf("[XX] error sending message: %d\n", result);
    }
}

int publish_nflog_data(struct nflog_g_handle *handle, struct nfgenmsg *msg, struct nflog_data *nfldata, void *mqtt_ptr)
{
    char* data;
    size_t datalen;
    struct nfulnl_msg_packet_hdr* phdr = nflog_get_msg_packet_hdr(nfldata);
    printf("%02x\n", phdr->hw_protocol);

    struct timeval tv;
    char ip_frm[INET6_ADDRSTRLEN];
    char ip_to[INET6_ADDRSTRLEN];


    //data = (char*)malloc(MAX);
    data = NULL;
    datalen = nflog_get_payload(nfldata, &data);
    nflog_get_timestamp(nfldata, &tv);


    printf("NFLOG event, payload size: %u, data:\n", (unsigned int)datalen);
    hexprint(data, datalen);
    fflush(stdout);
    if (ntohs(phdr->hw_protocol) == 0x86dd) {
        inet_ntop(AF_INET6, &data[8], ip_frm, INET6_ADDRSTRLEN);
        inet_ntop(AF_INET6, &data[24], ip_to, INET6_ADDRSTRLEN);
    } else {
        inet_ntop(AF_INET, &data[12], ip_frm, INET6_ADDRSTRLEN);
        inet_ntop(AF_INET, &data[16], ip_to, INET6_ADDRSTRLEN);
    }

    printf("From: %s\n", ip_frm);
    printf("To:   %s\n", ip_to);

    publish_blocked_event(mqtt_ptr, ip_frm, ip_to, tv.tv_sec);

    return 0;
}

struct mosquitto*
setup_mqtt_client() {
    struct mosquitto* mqtt = mosquitto_new(NULL, true, NULL);
    int result = mosquitto_connect(mqtt, "192.168.1.1", 1883, 10);
    if (result != MOSQ_ERR_SUCCESS) {
        printf("Error connecting to mqtt server\n");
        exit(1);
    }

    return mqtt;
}


int main(int argc, char** argv) {

    signal(SIGINT, cleanup);
    signal(SIGKILL, cleanup);

    mosquitto_lib_init();

    struct mosquitto* mqtt = setup_mqtt_client();

    setup_netlogger_loop(1, &publish_nflog_data, (void*)mqtt, &handle, mqtt);
    return 0;
}
