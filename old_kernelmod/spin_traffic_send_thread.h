#ifndef SPIN_TRAFFIC_SEND_THREAD
#define SPIN_TRAFFIC_SEND_THREAD

#include "spin_util.h"

#define MAX_TRAFFIC_CLIENTS 10

// This module handles the actual sending of traffic information
// over a netlink socket to the listening client(s)
//
// Per client it creates a worker thread to send the data; each
// thread sends a number of messages and then waits for the client
// to send a message back (so that the socket buffer is not immediately
// full during burst traffic)
//



// Structure for 1 entry in the send queue
typedef struct send_queue_entry_s {
	int msg_size;
	void* msg_data;
	struct send_queue_entry_s* next;
} send_queue_entry_t;

typedef struct {
	send_queue_entry_t* first;
	send_queue_entry_t* last;
	size_t sent;
	//spinlock_t lock;
} send_queue_t;

typedef struct {
	// TODO: some of these may not be needed anymore

	// the send socket
	struct sock* traffic_nl_sk;
	// the queue of messages to send
	send_queue_t* send_queue;
	// keep a count of the messages sent since the last ping from
	// the client; if this gets too large, wait a bit
	int msgs_sent_since_ping;
	// client of this message queue
	uint32_t client_port_id;
	// the thread to send the message queue
	struct task_struct* task_struct;
	// pointer back up to the full structure
	void* traffic_clients;

    struct rw_semaphore sem;
} traffic_client_t;


// collection of all connected clients
typedef struct {
	// the send socket
	struct sock* traffic_nl_sk;
	// array of all active clients
	traffic_client_t* clients[MAX_TRAFFIC_CLIENTS];
	// number of current clients
	int count;
	//spinlock_t lock;
    struct rw_semaphore sem;
} traffic_clients_t;


traffic_client_t* traffic_client_create(traffic_clients_t* traffic_clients, struct sock *traffic_nl_sk, uint32_t client_port_id);
void traffic_client_destroy(traffic_client_t* traffic_client);

traffic_clients_t* traffic_clients_create(struct sock* traffic_nl_sk);
void traffic_clients_destroy(traffic_clients_t* traffic_clients);

/**
 * Adds a new traffic client; if the client was already known
 * sets the 'msgs_since_ping' value to 0
 */
int traffic_clients_add(traffic_clients_t* traffic_clients, uint32_t client_port_id);

/**
 * Removes a traffic client
 */
int traffic_clients_remove(traffic_clients_t* traffic_clients, uint32_t client_port_id);

/**
 * Returns the current number of traffic clients
 */
int traffic_clients_count(traffic_clients_t* traffic_clients);

/**
 * Send a message to all current traffic clients
 */
void traffic_clients_send(traffic_clients_t* traffic_clients, int msg_size, void* msg_data);


#endif // SPIN_TRAFFIC_SEND_THREAD
