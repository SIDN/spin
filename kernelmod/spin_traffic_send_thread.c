#include <linux/kthread.h>

#include <linux/errno.h>
#include <linux/hrtimer.h>
#include <linux/inet.h>
#include <linux/interrupt.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netlink.h>
#include <linux/sched.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/time.h>
#include <linux/wait.h>


#include "spin_traffic_send_thread.h"

send_queue_entry_t* send_queue_entry_create(int msg_size, void* msg_data, uint32_t client_port_id) {
	send_queue_entry_t* entry = (send_queue_entry_t*) kmalloc(sizeof(send_queue_entry_t), __GFP_WAIT);
	entry->msg_size = msg_size;
	entry->msg_data = kmalloc(msg_size, __GFP_WAIT);
	memcpy(entry->msg_data, msg_data, msg_size);
	entry->next = NULL;
	return entry;
}

void send_queue_entry_destroy(send_queue_entry_t* entry) {
	kfree(entry->msg_data);
	kfree(entry);
}

send_queue_t* send_queue_create(void) {
	send_queue_t* queue = (send_queue_t*) kmalloc(sizeof(send_queue_t), __GFP_WAIT);

	spin_lock_init(&queue->lock);
	queue->first = NULL;
	queue->last = NULL;
	queue->sent = 0;

	return queue;
}

void send_queue_destroy(send_queue_t* queue) {
	send_queue_entry_t* entry, * next;
	spin_lock(&queue->lock);
	entry = queue->first;
	while (entry != NULL) {
		next = entry->next;
		send_queue_entry_destroy(entry);
		entry = next;
	}
	spin_unlock(&queue->lock);
	kfree(queue);
}

void send_queue_add(send_queue_t* queue, int msg_size, void* msg_data, uint32_t client_port_id) {
	send_queue_entry_t* new = send_queue_entry_create(msg_size, msg_data, client_port_id);

	spin_lock(&queue->lock);
	if (queue->first == NULL) {
		queue->first = new;
		queue->last = new;
	} else {
		queue->last->next = new;
		queue->last = new;
	}
	spin_unlock(&queue->lock);
}

// returns first entry, passes ownership
send_queue_entry_t* send_queue_pop(send_queue_t* queue) {
	send_queue_entry_t* popped;

	spin_lock(&queue->lock);
	if (queue->first == NULL) {
		popped = NULL;
	} else {
		popped = queue->first;
		queue->first = popped->next;
	}
	spin_unlock(&queue->lock);

	return popped;
}

size_t send_queue_size(send_queue_t* queue) {
	size_t result = 0;
	send_queue_entry_t* cur = NULL;
	spin_lock(&queue->lock);
	
	cur = queue->first;
	while (cur != NULL) {
		result++;
		cur = cur->next;
	}
	
	spin_unlock(&queue->lock);
	return result;
}

void send_queue_print(send_queue_t* queue) {
	send_queue_entry_t* cur;

	spin_lock(&queue->lock);
	cur = queue->first;

	while(cur != NULL) {
		//printk("%02lu:\t%d to %u\n", i, cur->msg_size, cur->client_port_id);
		cur = cur->next;
	}
	printk(KERN_INFO "[XX] END OF QUEUE\n");
	spin_unlock(&queue->lock);
}

// TODO: does port id arg make sense here?
int queue_send_netlink_message(traffic_client_t* traffic_client, int msg_size, void* msg_data, uint32_t client_port_id) {
    struct nlmsghdr *nlh;
    struct sk_buff* skb_out;

    //hexdump_k(msg_data, 0, msg_size);

    skb_out = nlmsg_new(msg_size, 0);
    if(!skb_out) {
        printk(KERN_ERR "Failed to allocate new skb\n");
        return -255;
    }

    nlh = nlmsg_put(skb_out, 0, 0, NLMSG_DONE, msg_size, NLM_F_REQUEST);
    NETLINK_CB(skb_out).dst_group = 0;

    memcpy(nlmsg_data(nlh), msg_data, msg_size);

    return nlmsg_unicast(traffic_client->traffic_nl_sk, skb_out, client_port_id);
}

int queue_send_netlink_error(traffic_client_t* traffic_client, int msg_size, void* msg_data, uint32_t client_port_id) {
    struct nlmsghdr *nlh;
    struct sk_buff* skb_out;

    //hexdump_k(msg_data, 0, msg_size);

    skb_out = nlmsg_new(msg_size, 0);
    if(!skb_out) {
        printk(KERN_ERR "Failed to allocate new skb\n");
        return -255;
    }

    nlh = nlmsg_put(skb_out, 0, 0, NLMSG_ERROR, msg_size, 0);
    NETLINK_CB(skb_out).dst_group = 0;

    memcpy(nlmsg_data(nlh), msg_data, msg_size);

    return nlmsg_unicast(traffic_client->traffic_nl_sk, skb_out, client_port_id);
}

#include "pkt_info.h"

void printk_msg(unsigned char* data, int size) {
	pkt_info_t pkt;
	dns_pkt_info_t dns_pkt;
	char pkt_str[1024];
	int type;
	type = wire2pktinfo(&pkt, data);
	if (type == SPIN_BLOCKED) {
		pktinfo2str(pkt_str, &pkt, 2048);
		printk("[BLOCKED] %s\n", pkt_str);
	} else if (type == SPIN_TRAFFIC_DATA) {
		pktinfo2str(pkt_str, &pkt, 2048);
		printk("[TRAFFIC] %s\n", pkt_str);
	} else if (type == SPIN_DNS_ANSWER) {
		// note: bad version would have been caught in wire2pktinfo
		// in this specific case
		wire2dns_pktinfo(&dns_pkt, data);
		dns_pktinfo2str(pkt_str, &dns_pkt, 2048);
		printk("[DNS] %s\n", pkt_str);
	} else {
		printk("[unknown packet sent]\n");
	}
}

int sender_thread(void *traffic_client_p) {
	traffic_client_t* traffic_client = (traffic_client_t*) traffic_client_p;
	send_queue_entry_t* entry;
	int res;
	int done = 0;
	int wait_counter = 0;

	//printk("[XX] Thread running\n");

	while (!kthread_should_stop() && !done) {
		//printk("[XX] Thread woke up\n");
		if (send_queue_size(traffic_client->send_queue) > 0) {
			while (traffic_client->msgs_sent_since_ping > 100 && !kthread_should_stop()) {
				printk("[XX] waiting to send until buffer clears. one moment please (client %d, buffer size %lu)\n", traffic_client->client_port_id, send_queue_size(traffic_client->send_queue));
				msleep(1000);
				wait_counter++;
				if (wait_counter > 25) {
					// oh well, try to send anyway
					traffic_client->msgs_sent_since_ping = 0;
				}
			}
			wait_counter = 0;
			entry = send_queue_pop(traffic_client->send_queue);
			//printk("[XX] queue for client %u has %lu messages\n", entry->client_port_id, send_queue_size(traffic_client->send_queue) + 1);
			if (entry != NULL) {
				//printk("[XX] sending message\n");
				// TODO: handle retry / error / etc.
				res = queue_send_netlink_message(traffic_client, entry->msg_size, entry->msg_data, traffic_client->client_port_id);
				if(res<0) {
					printk(KERN_INFO "Error sending data to client: %d\n", res);
					if (res == -11) {
						while (res == -11) {
							printk("wait a bit for queue to clear, then retry\n");
							msleep(2000);
							res = queue_send_netlink_message(traffic_client, entry->msg_size, entry->msg_data, traffic_client->client_port_id);
						}
					} else {
						// TODO: move client structures here as well (incl lock)
						// assume client is gone
						printk("[XX] Error with message, dropping client %u\n", traffic_client->client_port_id);
						// we are in the sender thread already, just destroy the structure and stop the thread from here
						traffic_clients_remove(traffic_client->traffic_clients, traffic_client->client_port_id);
						done = 1;
					}
				} else {
					traffic_client->msgs_sent_since_ping++;
					traffic_client->send_queue->sent++;
					
					//printk("[XX] msgs sent since reset: %d\n", traffic_client->msgs_sent_since_ping);
					//printk("[XX] msgs sent total: %lu\n", traffic_client->send_queue->sent);
					//printk_msg(entry->msg_data, entry->msg_size);
				}

				// we got ownership at the pop(), so delete
				send_queue_entry_destroy(entry);
			}
			schedule();
		} else {
			// TODO: is it useful to make this interruptible? perhaps
			// sleep forever until a new message comes in?
			msleep(1000);
		}
	}
	printk("[XX] thread loop stopped\n");
	return 0;
}

traffic_client_t* traffic_client_create(traffic_clients_t* traffic_clients, struct sock *traffic_nl_sk, uint32_t client_port_id) {
	traffic_client_t* traffic_client = (traffic_client_t*) kmalloc(sizeof(traffic_client_t), __GFP_WAIT);
	traffic_client->client_port_id = client_port_id;
	traffic_client->traffic_nl_sk = traffic_nl_sk;
	traffic_client->send_queue = send_queue_create();
	traffic_client->traffic_clients = traffic_clients;
	traffic_client->msgs_sent_since_ping = 0;

	// create and start the thread immediately
	traffic_client->task_struct = kthread_run(sender_thread,
	                                          traffic_client,
	                                          "handler thread");

	return traffic_client;
}

void traffic_client_destroy(traffic_client_t* traffic_client) {
	send_queue_destroy(traffic_client->send_queue);
	kfree(traffic_client);
}

void traffic_client_stop(traffic_client_t* handler_info) {
	// also rename to destroy, and change base name
	kthread_stop(handler_info->task_struct);
}

traffic_clients_t* traffic_clients_create(struct sock* traffic_nl_sk) {
	traffic_clients_t* clients = (traffic_clients_t*) kmalloc(sizeof(traffic_clients_t), __GFP_WAIT);
	clients->traffic_nl_sk = traffic_nl_sk;
	clients->count = 0;
	spin_lock_init(&clients->lock);
	return clients;
}

void traffic_clients_destroy(traffic_clients_t* traffic_clients) {
	// stop anny running client, then free up the data
	int i = 0;
	spin_lock(&traffic_clients->lock);
	for (i = 0; i < traffic_clients->count; i++) {
		// todo: _destroy for traffic_client_t
		// stop thread from destructor or here/separately?
		traffic_client_stop(traffic_clients->clients[i]);
		traffic_client_destroy(traffic_clients->clients[i]);
	}
	spin_unlock(&traffic_clients->lock);
	kfree(traffic_clients);
}

int traffic_clients_add(traffic_clients_t* traffic_clients, uint32_t client_port_id) {
	int i;
	//printk("add traffic client %u\n", client_port_id);
	spin_lock(&traffic_clients->lock);
	for (i = 0; i < traffic_clients->count; i++) {
		if (traffic_clients->clients[i]->client_port_id == client_port_id) {
			// already exists
			traffic_clients->clients[i]->msgs_sent_since_ping = 0;
			spin_unlock(&traffic_clients->lock);
			return i;
		}
	}
	if (traffic_clients->count < MAX_TRAFFIC_CLIENTS) {
		// ok, new
		traffic_clients->clients[traffic_clients->count] = traffic_client_create(traffic_clients, traffic_clients->traffic_nl_sk, client_port_id);
		printk("[XX] created data handler for client %d at %p\n", traffic_clients->count, traffic_clients->clients[traffic_clients->count]);
		traffic_clients->count++;
		spin_unlock(&traffic_clients->lock);
		return i;
	} else {
		// at max
		spin_unlock(&traffic_clients->lock);
		return 0;
	}
}

int traffic_clients_remove(traffic_clients_t* traffic_clients, uint32_t client_port_id) {
	int found = 0;
	int i;
	spin_lock(&traffic_clients->lock);
	for (i = 0; i < traffic_clients->count; i++) {
		if (!found) {
			if (traffic_clients->clients[i]->client_port_id == client_port_id) {
				found = 1;
				traffic_client_destroy(traffic_clients->clients[i]);
				if (i < traffic_clients->count) {
				    traffic_clients->clients[i] = traffic_clients->clients[i+1];
			    }
			    traffic_clients->count--;
			}
		} else {
			// move to prev
			traffic_clients->clients[i-1] = traffic_clients->clients[i];
		}
	}
	if (found) {
		printk("[XX] client removed. count now %u\n", traffic_clients->count);
	} else {
		printk("[XX] client was not found\n");
	}
	spin_unlock(&traffic_clients->lock);
	return found;
}

int traffic_clients_count(traffic_clients_t* traffic_clients) {
	return traffic_clients->count;
}

void traffic_clients_send(traffic_clients_t* traffic_clients, int msg_size, void* data) {
	int i;
	// add it to all queues
	spin_lock(&traffic_clients->lock);
	for (i = 0; i < traffic_clients->count; i++) {
		//printk("[XX] send message to client %d with port id %u\n", i, traffic_clients->clients[i].client_port_id);
		send_queue_add(traffic_clients->clients[i]->send_queue, msg_size, data, traffic_clients->clients[i]->client_port_id);
		//printk("[XX] queue size for %u: %lu\n", traffic_clients->clients[i]->client_port_id, send_queue_size(traffic_clients->clients[i]->send_queue));
	}
	spin_unlock(&traffic_clients->lock);
}

void traffic_clients_reset_msgs_count(traffic_clients_t* traffic_clients, uint32_t client_port_id) {
	int i;
	for (i = 0; i < traffic_clients->count; i++) {
		if (traffic_clients->clients[i]->client_port_id == client_port_id) {
			traffic_clients->clients[i]->msgs_sent_since_ping = 0;
		}
	}
}
