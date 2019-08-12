#ifndef SPIN_CORE2PUBSUB_H
#define SPIN_CORE2PUBSUB_H

#include "spindata.h"
#include "util.h"

void pubsub_publish(char *, int, const void*, int);
void core2pubsub_publish(buffer_t *);
void core2pubsub_publish_chan(char *channel, spin_data sd, int retain); 
void init_mosquitto(const char* , int );
void finish_mosquitto();

void broadcast_iplist(int iplist, const char* list_name);

#endif // SPIN_CORE2PUBSUB_H
