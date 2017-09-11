
#include "spin_util.h"

char* ipv4_val = "4";
char* ipv6_val = "6";

static int verbosity = 0;

ip_store_t* ip_store_create() {
    ip_store_t* ip_store = (ip_store_t*) kmalloc(sizeof(ip_store_t), __GFP_WAIT);
    ip_store->elements = NULL;

    return ip_store;
}

void ip_store_destroy(ip_store_t* ip_store) {
    ip_store_el_t* el = ip_store->elements;
    while (el != NULL) {
        el = el->next;
        kfree(el);
    }
    kfree(ip_store);
}

static inline void split_key(unsigned char ip[16], u64* k1, u64* k2) {
    memcpy(k1, ip, 8);
    memcpy(k2, ip+8, 8);
}

int ip_store_contains_ip(ip_store_t* ip_store, unsigned char ip[16]) {
    u64 k1, k2;
    ip_store_el_t* el = ip_store->elements;
    split_key(ip, &k1, &k2);
    while (el != NULL) {
        if (k1 == el->k1 && k2 == el->k2) {
            return 1;
        }
        el = el->next;
    }
    return 0;
}

void ip_store_add_ip(ip_store_t* ip_store, int ipv6, unsigned char ip[16]) {
    u64 k1, k2;
    char* ipv = ipv6 ? ipv6_val : ipv4_val;
    ip_store_el_t* el;
    ip_store_el_t* el_new;

    if (!ip_store_contains_ip(ip_store, ip)) {
        split_key(ip, &k1, &k2);
        el_new = (ip_store_el_t*)kmalloc(sizeof(ip_store_el_t), __GFP_WAIT);
        el_new->k1 = k1;
        el_new->k2 = k2;
        el_new->val = ipv;
        el_new->next = NULL;
        el = ip_store->elements;
        if (el == NULL) {
            ip_store->elements = el_new;
        } else {
            while (el->next != NULL) {
                el = el->next;
            }
            el->next = el_new;
        }
    }
}

void ip_store_remove_ip(ip_store_t* ip_store, unsigned char ip[16]) {
    u64 k1, k2;
    ip_store_el_t* prev_el;
    ip_store_el_t* el = ip_store->elements;

    if (el == NULL) {
        return;
    }
    split_key(ip, &k1, &k2);
    if (el->k1 == k1 && el->k2 == k2) {
        ip_store->elements = el->next;
        kfree(el);
        return;
    }
    prev_el = el;
    el = el->next;
    while (el != NULL) {
        if (el->k1 == k1 && el->k2 == k2) {
            prev_el->next = el->next;
            kfree(el);
            return;
        }
        prev_el = el;
        el = el->next;
    }
    return;
}

void ip_store_for_each(ip_store_t* ip_store,
                       void(*cb)(unsigned char[16], int, void* data),
                       void* data) {
    unsigned char ip[16];
    ip_store_el_t* el = ip_store->elements;

    while (el != NULL) {
        memcpy(ip, &el->k1, 8);
        memcpy(ip+8, &el->k2, 8);
        cb(ip, el->val == ipv6_val, data);
        el = el->next;
    }
}

void
log_set_verbosity(int new_verbosity) {
    verbosity = new_verbosity;
}

int
log_get_verbosity() {
    return verbosity;
}

int*
log_get_verbosity_ptr() {
    return &verbosity;
}

//int log_get_verbosity

void
log_packet(pkt_info_t* pkt_info) {
    char pkt_str[INET6_ADDRSTRLEN];
    pktinfo2str(pkt_str, pkt_info, INET6_ADDRSTRLEN);
    printk("%s\n", pkt_str);
}

void
printv(int module_verbosity, const char* format, ...) {
    va_list args;
    if (verbosity >= module_verbosity) {
        va_start(args, format);
        vprintk(format, args);
        va_end(args);
    }
}

void hexdump_k(uint8_t* data, unsigned int offset, unsigned int size) {
    unsigned int i;
    printv(5, KERN_DEBUG "%02u: ", 0);
    for (i = 0; i < size; i++) {
        if (i > 0 && i % 10 == 0) {
            printv(5, KERN_DEBUG "\n%02u: ", i);
        }
        printv(5, KERN_DEBUG "%02x ", data[i + offset]);
    }
    printv(5, KERN_DEBUG "\n");
}
