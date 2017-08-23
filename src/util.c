#include "util.h"

#include <assert.h>

#include <errno.h>
#include <stdio.h>

#define clean_errno() (errno == 0 ? "None" : strerror(errno))
#define log_error(M, ...) fprintf(stderr, "[ERROR] (%s:%d: errno: %s) " M "\n", __FILE__, __LINE__, clean_errno(), ##__VA_ARGS__)
#define assertf(A, M, ...) if(!(A)) {log_error(M, ##__VA_ARGS__); assert(A); }

int cmp_ints(size_t size_a, const void* key_a, size_t size_b, const void* key_b) {
    int *a, *b;
    assert(size_a == sizeof(*a));
    assert(size_b == sizeof(*b));
    a = (int*) key_a;
    b = (int*) key_b;
    if (*a > *b) {
        return 1;
    } else if (*a < *b) {
        return -1;
    } else {
        return 0;
    }
}

int cmp_strs(size_t size_a, const void* key_a, size_t size_b, const void* key_b) {
    size_t size = size_a;
    int result;

    if (size_b < size) {
        size = size_b;
    }
    result = memcmp(key_a, key_b, size);
    if (result == 0) {
        if (size_a > size_b) {
            return 1;
        } else if (size_a < size_b) {
            return -1;
        }
    } else {
        if (result > 0) {
            return 1;
        } else if (result < 0) {
            return -1;
        }
    }
    return 0;
}

// hmm, we define these in node_cache as well. move to tree itself? (as helper functions?)
int cmp_ips(size_t size_a, const void* key_a, size_t size_b, const void* key_b) {
    ip_t* ip_a = (ip_t*) key_a;
    ip_t* ip_b = (ip_t*) key_b;
    assertf((size_a == sizeof(ip_t)), "key_a is not of size of ip_t but %lu", size_a);
    assertf((size_b == sizeof(ip_t)), "key_b is not of size of ip_t but %lu", size_b);
    if (ip_a->family < ip_b->family) {
        return -1;
    } else if (ip_a->family > ip_b->family) {
        return 1;
    } else {
        return memcmp(ip_a->addr, ip_b->addr, 16);
    }
}


// see above
// note, this does not take label order into account, it is just on pure bytes
int cmp_domains(size_t size_a, const void* a, size_t size_b, const void* b) {
    size_t s = size_a;
    int result;

    if (s > size_b) {
        s = size_b;
    }
    result = memcmp(a, b, s);
    if (result == 0) {
        if (size_a > size_b) {
            return -1;
        } else if (size_a < size_b) {
            return 1;
        }
    }
    if (result > 0) {
        return 1;
    } else if (result < 0) {
        return -1;
    }
    return 0;
}

int cmp_pktinfos(size_t size_a, const void* a, size_t size_b, const void* b) {
    assert(size_a == 38);
    assert(size_b == 38);
    return memcmp(a, b, 38);
}

int
spin_pton(ip_t* ip, const char* ip_str) {
    if (ip == NULL) {
        return 0;
    }

    int result = inet_pton(AF_INET6, ip_str, &ip->addr);
    if (result == 1) {
        ip->family = (uint8_t)AF_INET6;
    } else {
        result = inet_pton(AF_INET, ip_str, &ip->addr[12]);
        if (result == 1) {
            ip->family = AF_INET;
            memset(ip->addr, 0, 12);
        } else {
            return 0;
        }
    }
    return 1;
}

size_t
spin_ntop(char* dest, ip_t* ip, size_t size) {
    if (ip->family == AF_INET) {
        inet_ntop(ip->family, &ip->addr[12], dest, size);
        return strlen(dest);
    } else {
        inet_ntop(ip->family, ip->addr, dest, size);
        return strlen(dest);
    }
}

buffer_t* buffer_create(size_t size) {
    buffer_t* buf = malloc(sizeof(buffer_t));
    buf->data = malloc(size);
    buf->max = size;
    buffer_reset(buf);
    return buf;
}

void buffer_destroy(buffer_t* buffer) {
    free(buffer->data);
    free(buffer);
}

int buffer_finish(buffer_t* buffer) {
    buffer->finished = 1;
    buffer->data[buffer->pos] = '\0';
    return buffer->ok;
}

void buffer_reset(buffer_t* buffer) {
    buffer->pos = 0;
    buffer->finished = 0;
    buffer->ok = 1;
}

size_t buffer_size(buffer_t* buffer) {
    return buffer->pos;
}

char* buffer_str(buffer_t* buffer) {
    // check if finished?
    assert(buffer->finished);
    return buffer->data;
}

int buffer_write(buffer_t* buffer, const char* format, ...) {
    va_list args;
    int written = 0;
    size_t remaining;

    if (!buffer->ok || buffer->finished) {
        return -1;
    }

    if (buffer->max < buffer->pos) {
        return -1;
    }
    remaining = buffer->max - buffer->pos;
    va_start(args, format);
    written = vsnprintf(buffer->data + buffer->pos, remaining, format, args);
    va_end(args);
    if (written == -1 || written+buffer->pos >= buffer->max) {
        buffer->ok = 0;
        return -1;
    } else {
        buffer->pos += written;
    }
    return 0;
}

int
buffer_ok(buffer_t* buffer) {
    return buffer->ok == 1;
}

int store_ip_tree(tree_t* tree, const char* filename) {
    tree_entry_t* cur;
    char ip_str[INET6_ADDRSTRLEN];

    FILE* out = fopen(filename, "w");
    if (out == NULL) {
        return 0;
    }
    cur = tree_first(tree);
    while (cur != NULL) {
        spin_ntop(ip_str, cur->key, cur->key_size);
        fprintf(out, "%s\n", ip_str);
        cur = tree_next(cur);
    }
    fclose(out);
    return 1;
}

#define LINE_MAX 1024
int read_ip_tree(tree_t* dest, const char* filename) {
    int count = 0;
    char* line;
    char* rline;
    ip_t ip;

    FILE* in = fopen(filename, "r");
    if (in == NULL) {
        return -1;
    }
    line = malloc(LINE_MAX);
    rline = fgets(line, LINE_MAX, in);
    while (rline != NULL) {
        if (index(rline, '\n') >= 0) {
            *index(rline, '\n') = '\0';
            if (spin_pton(&ip, line)) {
                tree_add(dest, sizeof(ip), &ip, 0, NULL, 1);
                count++;
            }
        }
        rline = fgets(line, LINE_MAX, in);
    }
    free(line);
    fclose(in);
    return count;
}
