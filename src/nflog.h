#ifndef NFLOG_H
#define NFLOG_H 1

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <libnetfilter_log/linux_nfnetlink_log.h>

#include <libnfnetlink/libnfnetlink.h>
#include <libnetfilter_log/libnetfilter_log.h>

#include <mosquitto.h>

/* public interface */
struct nflog_g_handle
{
        struct nflog_g_handle *next;
        struct nflog_handle *h;
        u_int16_t id;

        nflog_callback *cb;
        void *data;
};

void nflog_cleanup_handle(struct nflog_g_handle* ghandle);

void setup_netlogger_loop(int groupnum, nflog_callback* cb, void* cb_data, struct nflog_g_handle** ghandle, struct mosquitto* mqtt);


#endif // NFLOG_H
