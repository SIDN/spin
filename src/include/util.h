#ifndef SPIN_UTIL_H
#define SPIN_UTIL_H 1

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "tree.h"

int cmp_ints(size_t size_a, const void* key_a, size_t size_b, const void* key_b);
int cmp_2ints(size_t size_a, const void* key_a, size_t size_b, const void* key_b);
int cmp_strs(size_t size_a, const void* key_a, size_t size_b, const void* key_b);
int cmp_ipdata_raw(size_t size_a, const void* key_a, size_t size_b, const void* key_b);
int cmp_ips(size_t size_a, const void* key_a, size_t size_b, const void* key_b);
int cmp_domains(size_t size_a, const void* a, size_t size_b, const void* b);
int cmp_pktinfos(size_t size_a, const void* a, size_t size_b, const void* b);

typedef struct {
    uint8_t family;
    uint8_t netmask;
    uint8_t addr[16];
} ip_t;

int spin_pton(ip_t* dest, const char* ip);
size_t spin_ntop(char* dest, ip_t* ip, size_t dest_size);

typedef struct {
    char* data;
    size_t pos;
    size_t max;
    int finished;
    int ok;
    int allow_resize;
} buffer_t;

void phexdump(const uint8_t* data, unsigned int size);

buffer_t* buffer_create(size_t size);
void buffer_destroy(buffer_t* buffer);


void buffer_allow_resize(buffer_t* buffer);
int buffer_finish(buffer_t* buffer);
void buffer_reset(buffer_t* buffer);
size_t buffer_size(buffer_t* buffer);
char* buffer_str(buffer_t* buffer);

int buffer_write(buffer_t* buffer, const char* format, ...) __attribute__((__format__ (printf, 2, 3)));

int buffer_ok(buffer_t* buffer);

/*
 * Stores a tree of IP values in the given files
 *
 * The tree should be keyed by the IP values and the tree data is
 * ignored
 * Returns 1 on success, 0 on failure.
 */
int store_ip_tree(tree_t* tree, const char* filename);
/*
 * Same for nodepair
 */
int store_nodepair_tree(tree_t* tree, const char* filename);

/*
 * Read the given filename, which should consist of one IP string
 * per line, and add them with NULL data to the given tree
 *
 * Returns the number of IP values read (note; does not look at
 * duplicates, so the number of elements added to the tree may be
 * different from the number of items read)
 */
int read_ip_tree(tree_t* dest, const char* filename);

void hexdump(uint8_t* data, unsigned int size);

int ip_in_net(ip_t* ip, ip_t* net);

/*
 * Set IP address data in an ip_t structure
 * The dest MUST be allocated with sizeof(ip_t) bytes of memory
 * 
 * If netmask is not 0, it is set too
 * If netmask is 0, it is set to either 32 (ipv4) or 128 (ipv6)
 * ip_data should point to a memory structure that has the ip address
 * in ip_t format; e.g. 16 octets of data for ipv6, or 12 zero bytes
 * followed by 4 octets of data for ipv4
 */
void copy_ip_data(ip_t* dest, int family, int netmask, void* ip_data);

#endif // SPIN_UTIL_H
