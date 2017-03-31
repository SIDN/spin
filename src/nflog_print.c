/*
 * Simple program that listens for NFLOG messages and prints some data
 * about them
 *
 * (c) 2017 SIDN
 */

#include <arpa/inet.h>
#include <signal.h>

#include "nflog.h"

// global pointer for cleanup on exit
static struct nflog_g_handle* handle;

void cleanup() {
    nflog_cleanup_handle(handle);
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

int print_nflog_data(struct nflog_g_handle *handle, struct nfgenmsg *msg, struct nflog_data *nfldata, void *foo)
{
    char* data;
    size_t datalen;
    struct nfulnl_msg_packet_hdr* phdr = nflog_get_msg_packet_hdr(nfldata);
    printf("%02x\n", phdr->hw_protocol);

    char ip_frm[INET6_ADDRSTRLEN];
    char ip_to[INET6_ADDRSTRLEN];

    //data = (char*)malloc(MAX);
    data = NULL;
    datalen = nflog_get_payload(nfldata, &data);

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

    printf("%02x\n", nflog_get_hwtype(nfldata));

    return 0;
}


int main(int argc, char** argv) {
    signal(SIGINT, cleanup);
    signal(SIGKILL, cleanup);
    setup_netlogger_loop(1, &print_nflog_data, NULL, &handle, NULL);
    return 0;
}
