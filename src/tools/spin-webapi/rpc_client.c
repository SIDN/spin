/*
 * This module handles SPIN web api-specific RPC requests to the main
 * spin daemon;
 * - put through web api json-rpc client requests to spin directly
 * - add custom function for requesting specific data about devices
 */

#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>


// Sends the given string to the rpc domain socket
// returns the response
// caller must free response data
char*
send_jsonrpc_message(const char* request) {
    size_t response_size=1024;
    char* response;// = malloc(response_size);
    const char* domain_socket_path = "/var/run/spin_rpc.sock";
    
    struct sockaddr_un addr;
    int fd;
    ssize_t rc;
    size_t data_read;
    size_t data_size;
    
    if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1){
        fprintf(stderr, "Error connecting to domain socket %s", domain_socket_path);
        return NULL;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (*domain_socket_path == '\0') {
        *addr.sun_path = '\0';
        strncpy(addr.sun_path+1, domain_socket_path+1, sizeof(addr.sun_path)-2);
    } else {
        strncpy(addr.sun_path, domain_socket_path, sizeof(addr.sun_path)-1);
    }

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        fprintf(stderr, "Error connecting to JSONRPC socket");
        exit(-1);
    }

    data_read = 0;
    data_size = strlen(request);
    while (data_read < data_size) {
        rc = write(fd, request, data_size - data_read);
        if (rc < 0) {
            fprintf(stderr, "Error while writing");
            return NULL;
        }
        data_read += rc;
    }
    fprintf(stdout, "Message written, reading response\n");

    data_read = 0;
    response = malloc(response_size);
    if (response == NULL) {
        fprintf(stderr, "[XX] out of memory\n");
        return NULL;
    }
    rc = 1;
    while(rc != 0) {
        rc = read(fd, response + data_read, response_size - data_read);
        if (rc < 0) {
            fprintf(stderr, "Error while reading\n");
            free(response);
            return NULL;
        } else if (rc > 0) {
            data_read += rc;
        }
    }
    response[data_read] = '\0';
    close(fd);
    return response;
}

