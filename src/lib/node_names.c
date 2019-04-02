
#include "node_names.h"

#include <stdio.h>
#include <errno.h>
#include <string.h>

node_names_t* node_names_create(void) {
    node_names_t* node_names = (node_names_t*) malloc(sizeof(node_names_t));
    node_names->user_names_by_ip = tree_create(cmp_ips);
    node_names->user_names_by_mac = tree_create(cmp_strs);
    node_names->dhcp_names_by_ip = tree_create(cmp_ips);
    node_names->dhcp_names_by_mac = tree_create(cmp_strs);
    return node_names;
}

void node_names_destroy(node_names_t* node_names) {
    tree_destroy(node_names->user_names_by_ip);
    tree_destroy(node_names->user_names_by_mac);
    tree_destroy(node_names->dhcp_names_by_ip);
    tree_destroy(node_names->dhcp_names_by_mac);
    free(node_names);
}

// 0 on eof
// >0 on something read (even if just \n)
static inline int readline(char* dest, FILE* in, size_t max) {
    size_t read = 0;
    while (1) {
        int c = getc(in);
        switch (c) {
        case EOF:
            return read;
            break;
        case '\n':
            dest[read++] = '\0';
            return read;
            break;
        default:
            dest[read++] = c;
            break;
        }

        if (read >= max - 1) {
            dest[++read] = '\0';
            return read;
        }
    }
}

#define NODE_NAMES_LINE_MAX 256
#define TOKEN_MAX 64

static inline char* find_name(const char* str) {
    // find the separator
    char* result = strpbrk(str, " \t");
    if (result != NULL) {
        while (result[0] == " "[0] || result[0] == "\t"[0]) {
            result++;
        }
    }
    return result;
}

static inline char* find_mac(const char* str) {
    // find the separator
    char* result = strpbrk(str, " \t");
    if (result != NULL) {
        while (result[0] == " "[0] || result[0] == "\t"[0]) {
            result++;
        }
    }
    return result;
}

static inline int instr(char c, const char* str) {
    return index(str, c) != NULL;
}

static inline size_t skip_token(char* in, const char* delimit, size_t max) {
    size_t in_i = 0;
    // skip spaces
    while (instr(in[in_i], " \t")) { in_i++; };
    while (!instr(in[in_i], delimit)) {
        in_i++;
        if (in_i >= max) {
            return 0;
        }
    }
    return in_i;
}

static inline size_t get_token(char* dest, char* in, const char* delimit, size_t max) {
    size_t in_i = 0, out_i = 0;
    // skip spaces
    while (instr(in[in_i], " \t")) { in_i++; };
    while (!instr(in[in_i], delimit)) {
        dest[out_i++] = in[in_i++];
        if (out_i >= max) {
            return 0;
        }
    }
    dest[out_i++] = '\0';
    return out_i;
}

static inline size_t unquote_token(char* token) {
    size_t i;
    char quote;
    if (token[0] == '"') {
        quote = '"';
    } else if (token[0] == '\'') {
        quote = '\'';
    } else {
        return strlen(token + 1);
    }
    for (i = 1; i < strlen(token); i++) {
        if (token[i] == quote && token[i-1] != '\\') {
            token[i - 1] = '\0';
            break;
        } else {
            token[i - 1] = token[i];
        }
    }
    return i;
}

int node_names_read_dhcpconfig(node_names_t* node_names, const char* filename) {
    // states:
    // 0: looking for 'config' section
    // 1: in 'config host' section
    int done = 0;
    int state = 0;
    char* line;
    char* line_cur;
    int line_size;
    char* token;
    size_t token_len;
    char* token2;

    ip_t ip;
    ip.family = 0;
    char* mac = NULL;
    char* name = NULL;

    FILE* in = fopen(filename, "r");

    if (in == NULL) {
        return -1;
    }
    line = (char*)malloc(NODE_NAMES_LINE_MAX);
    token = (char*)malloc(NODE_NAMES_LINE_MAX);
    token2 = (char*)malloc(NODE_NAMES_LINE_MAX);

    while (!done) {
        line_size = readline(line, in, NODE_NAMES_LINE_MAX);
        if (line_size == 0) {
            done = 1;
            break;
        }

        switch (state) {
        case 0:
            if (strncmp(line, "config host", 11) == 0) {
                state = 1;
            }
            break;
        case 1:
            if (strncmp(line, "config ", 7) == 0) {
                // handle, switch state
                if (name != NULL && ip.family != 0) {
                    tree_add(node_names->dhcp_names_by_ip, sizeof(ip_t), &ip, strlen(name)+1, name, 1);
                }
                if (name != NULL && mac != NULL) {
                    tree_add(node_names->dhcp_names_by_mac, strlen(mac)+1, mac, strlen(name)+1, name, 1);
                }
                free(name);
                free(mac);
                name = NULL;
                mac = NULL;
                ip.family = 0;

                if (strncmp(line, "config host", 11) == 0) {
                    // state stays 1
                } else {
                    state = 0;
                }
            }
            line_cur = line;
            token_len = get_token(token, line_cur, " \t", NODE_NAMES_LINE_MAX);
            if (strncmp(token, "option", 6) != 0) {
                break;
            }
            line_cur += token_len;
            token_len = get_token(token, line_cur, " \t", NODE_NAMES_LINE_MAX);
            line_cur += token_len;
            get_token(token2, line_cur, "\r\n", NODE_NAMES_LINE_MAX);
            unquote_token(token2);
            if (strcmp(token, "name") == 0) {
                if (name != NULL) {
                    free(name);
                }
                name = strdup(token2);
            } else if (strcmp(token, "ip") == 0) {
                if (!spin_pton(&ip, token2)) {
                    ip.family = 0;
                }
            } else if (strcmp(token, "mac") == 0) {
                if (mac != NULL) {
                    free(mac);
                }
                mac = strdup(token2);
            }
            break;
        default:
            goto done;
        }
    }

    done:
    // handle if any left
    if (name != NULL && ip.family != 0) {
        tree_add(node_names->dhcp_names_by_ip, sizeof(ip_t), &ip, strlen(name)+1, name, 1);
    }
    if (name != NULL && mac != NULL) {
        tree_add(node_names->dhcp_names_by_mac, strlen(mac)+1, mac, strlen(name)+1, name, 1);
    }
    free(name);
    free(mac);

    fclose(in);
    free(token);
    free(token2);
    free(line);
    return 0;
}

int
node_names_read_dhcpleases(node_names_t* node_names, const char* filename) {
    // states:
    // 0: looking for 'config' section
    // 1: in 'config host' section
    int done = 0;
    char* line;
    char* line_cur;
    int line_size;
    size_t token_len;

    char mac[TOKEN_MAX];
    char name[TOKEN_MAX];

    FILE* in = fopen(filename, "r");

    if (in == NULL) {
        return -1;
    }
    line = (char*)malloc(NODE_NAMES_LINE_MAX);

    while (!done) {
        line_size = readline(line, in, NODE_NAMES_LINE_MAX);
        if (line_size == 0) {
            done = 1;
            break;
        }
        line_cur = line;

        // Assuming the simplified lease format for now:
        // timestamp mac ip name [other]
        token_len = skip_token(line_cur, " \t", NODE_NAMES_LINE_MAX);
        if (token_len <= 0) {
            done = 1;
            break;
        }
        line_cur += token_len;
        token_len = get_token(mac, line_cur, " \t", TOKEN_MAX);
        if (token_len <= 0) {
            done = 1;
            break;
        }
        line_cur += token_len;
        token_len = skip_token(line_cur, " \t", NODE_NAMES_LINE_MAX);
        if (token_len <= 0) {
            done = 1;
            break;
        }
        line_cur += token_len;
        token_len = get_token(name, line_cur, " \t", TOKEN_MAX);
        if (token_len <= 0) {
            done = 1;
            break;
        }

        if (strlen(mac) != 0 && strlen(name) != 0 && strncmp(name, "*", 2) != 0) {
            tree_add(node_names->dhcp_names_by_mac, strlen(mac)+1, mac, strlen(name)+1, name, 1);
        }
        name[0] = '\0';
        mac[0] = '\0';
    }
    fclose(in);
    return 0;
}

int node_names_read_userconfig(node_names_t* node_names, const char* filename) {
    char* line;
    int line_size;
    ip_t ip;
    char token1[INET6_ADDRSTRLEN];
    size_t token1_len;
    char* token2;
    size_t token2_len;

    FILE* in = fopen(filename, "r");
    if (in == NULL) {
        fprintf(stderr, "Error: unable to read %s: %s\n", filename, strerror(errno));
        return -1;
    }
    line = (char*)malloc(NODE_NAMES_LINE_MAX);
    token2 = (char*)malloc(NODE_NAMES_LINE_MAX);
    while (1) {
        line_size = readline(line, in, NODE_NAMES_LINE_MAX);
        if (line_size == 0) {
            break;
        }
        if (strncmp(line, "name: ", 6) == 0) {
            token1_len = get_token(token1, line + 6, " \t", INET6_ADDRSTRLEN);
            token2_len = get_token(token2, line + 6 + token1_len, "\r\n", NODE_NAMES_LINE_MAX);
            if (token1_len == 0 || token2_len == 0) {
                continue;
            }
            if (spin_pton(&ip, token1)) {
                tree_add(node_names->user_names_by_ip, sizeof(ip_t), &ip, token2_len, token2, 1);
            } else if (token1_len == 18) {
                // should we have more checks or just put it in?
                // assume mac
                tree_add(node_names->user_names_by_mac, token1_len, token1, token2_len, token2, 1);
            }
        }
    }

    fclose(in);
    free(line);
    free(token2);
    return 0;
}

int
node_names_write_userconfig(node_names_t* node_names, const char* filename) {
    tree_entry_t* cur;
    FILE* output_file = fopen(filename, "w");
    char ip_str[INET6_ADDRSTRLEN];

    if (output_file == NULL) {
        return -1;
    }

    cur = tree_first(node_names->user_names_by_ip);
    while (cur != NULL) {
        spin_ntop(ip_str, cur->key, INET6_ADDRSTRLEN);
        fprintf(output_file, "name: %s %s\n", ip_str, (char*)cur->data);
        cur = tree_next(cur);
    }
    cur = tree_first(node_names->user_names_by_mac);
    while (cur != NULL) {
        fprintf(output_file, "name: %s %s\n", (char*)cur->key, (char*)cur->data);
        cur = tree_next(cur);
    }

    fclose(output_file);

    return 0;
}

char* node_names_find_ip(node_names_t* node_names, ip_t* ip) {
    tree_entry_t* entry = tree_find(node_names->user_names_by_ip, sizeof(ip_t), ip);
    if (entry == NULL) {
        entry = tree_find(node_names->dhcp_names_by_ip, sizeof(ip_t), ip);
    }
    if (entry == NULL) {
        return NULL;
    } else {
        return (char*) entry->data;
    }
}

char* node_names_find_mac(node_names_t* node_names, char* mac) {
    tree_entry_t* entry = tree_find(node_names->user_names_by_mac, strlen(mac) + 1, mac);
    if (entry == NULL) {
        entry = tree_find(node_names->dhcp_names_by_mac, strlen(mac) + 1, mac);
    }
    if (entry == NULL) {
        return NULL;
    } else {
        return (char*) entry->data;
    }
}

void node_names_add_user_name_ip(node_names_t* node_names, ip_t* ip, char* name) {
    if (name != NULL && ip != NULL) {
        tree_add(node_names->user_names_by_ip, sizeof(ip_t), ip, strlen(name)+1, name, 1);
    }
}

void node_names_add_user_name_mac(node_names_t* node_names, char* mac, char* name) {
    if (name != NULL && mac != NULL) {
        tree_add(node_names->user_names_by_mac, strlen(mac) + 1, mac, strlen(name)+1, name, 1);
    }
}

