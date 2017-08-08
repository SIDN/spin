#ifndef SPIN_UTIL_H
#define SPIN_UTIL_H 1

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

int cmp_ints(size_t size_a, const void* key_a, size_t size_b, const void* key_b);
int cmp_strs(size_t size_a, const void* key_a, size_t size_b, const void* key_b);
int cmp_ips(size_t size_a, const void* key_a, size_t size_b, const void* key_b);
int cmp_domains(size_t size_a, const void* a, size_t size_b, const void* b);
int cmp_pktinfos(size_t size_a, const void* a, size_t size_b, const void* b);

int spin_pton(uint8_t* dest, const char* ip);
unsigned int spin_ntop(char* dest, uint8_t* ip, socklen_t size);

typedef struct {
    char* data;
    size_t pos;
    size_t max;
    int finished;
    int ok;
} buffer_t;

buffer_t* buffer_create(size_t size);
void buffer_destroy(buffer_t* buffer);


int buffer_finish(buffer_t* buffer);
void buffer_reset(buffer_t* buffer);
size_t buffer_size(buffer_t* buffer);
char* buffer_str(buffer_t* buffer);

int buffer_write(buffer_t* buffer, const char* format, ...);

#endif // SPIN_UTIL_H
