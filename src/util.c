#include "util.h"

#include <assert.h>

#include <errno.h>

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
        } else {
            return 0;
        }
    } else {
        return result;
    }
}

// hmm, we define these in node_cache as well. move to tree itself? (as helper functions?)
int cmp_ips(size_t size_a, const void* key_a, size_t size_b, const void* key_b) {
    assertf((size_a == 17), "key_a is not of size 17 but %lu", size_a);
    assertf((size_b == 17), "key_b is not of size 17 but %lu", size_b);;
    return memcmp(key_a, key_b, 17);
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
        } else {
            return 0;
        }
    }
    return result;
}

int cmp_pktinfos(size_t size_a, const void* a, size_t size_b, const void* b) {
    assert(size_a == 38);
    assert(size_b == 38);
    return memcmp(a, b, 38);
}


// dest must have 17 bytes available
int spin_pton(uint8_t* dest, const char* ip) {
    int result = inet_pton(AF_INET6, ip, &dest[1]);
    if (result == 1) {
        dest[0] = AF_INET6;
    } else {
        result = inet_pton(AF_INET, ip, &dest[13]);
        if (result == 1) {
            dest[0] = AF_INET;
            memset(&dest[1], 0, 12);
        } else {
            return 0;
        }
    }
    return 1;
}

unsigned int spin_ntop(char* dest, uint8_t* ip, socklen_t size) {
    if (ip[0] == AF_INET) {
        inet_ntop(ip[0], &ip[13], dest, size);
        return strlen(dest);
    } else {
        inet_ntop(ip[0], &ip[1], dest, size);
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

