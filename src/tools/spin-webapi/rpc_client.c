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

#include <cJSON.h>
#include <spindata.h>


/*
 * Helper functions
 */

// Sends the given string to the rpc domain socket
// returns the response
// caller must free response data
char*
send_jsonrpc_message_raw(const char* request) {
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

spin_data
rpcc_create_request() {
    cJSON* request = cJSON_CreateObject();
    cJSON_AddStringToObject(request, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(request, "id", 1);
    return request;
}

void
rpcc_print_json(spin_data json) {
    char *string = cJSON_Print(json);
    fprintf(stdout, "[XX] JSON: %s\n", string);
    free(string);
}

/*
 * Basic lowlever requests that return spin_data objects
 */
spin_data
rpcc_send_request(spin_data request) {
    char* request_str = cJSON_Print(request);
    char* response_str = send_jsonrpc_message_raw(request_str);
    cJSON* response = cJSON_Parse(response_str);
    
    // TODO: check id and jsonrpc version?
    cJSON* result = cJSON_DetachItemFromObject(response, "result");

    cJSON_Delete(request);
    cJSON_Delete(response);
    free(request_str);
    free(response_str);
    
    rpcc_print_json(result);

    return result;
}

spin_data
rpcc_list_devices() {
    
    cJSON* request = rpcc_create_request();
    cJSON_AddStringToObject(request, "method", "list_devices");
    
    rpcc_print_json(request);

    cJSON* response = rpcc_send_request(request);

    return response;
}

spin_data
rpcc_get_device_by_mac(const char* device_mac) {
    cJSON* device;
    cJSON* result = NULL;
    cJSON* devices = rpcc_list_devices();
    int index = 0;
    cJSON_ArrayForEach(device, devices) {
        char* cur_device_mac = cJSON_GetObjectItem(device, "mac")->valuestring;
        if (strcmp(device_mac, cur_device_mac) == 0) {
            result = cJSON_DetachItemFromArray(devices, index);
        }
        index++;
    }
    cJSON_Delete(devices);
    return result;
}

/*
 * High-level functions that return specific data retrieved from JSONRPC
 */

/*
 * High-level functions to extract specific data from known spin_data (/cJSON) structures
 */
char* rpcc_get_device_name(spin_data device) {
    char* name = NULL;

    char* cur_device_mac = cJSON_GetObjectItem(device, "mac")->valuestring;
    cJSON* cur_device_name = cJSON_GetObjectItem(device, "name");
    if (cur_device_name != NULL) {
        name = strdup(cur_device_name->valuestring);
    } else {
        name = strdup(cur_device_mac);
    }
    return name;
}

char* rpcc_get_device_ips_as_string(spin_data device) {
    cJSON* ip;
    char* ips_string = NULL;

    cJSON* cur_device_ips = cJSON_GetObjectItem(device, "ips");
    if (cur_device_ips != NULL) {
        // ip max len is 16, add 2 for comma separator
        ips_string = malloc(18*cJSON_GetArraySize(cur_device_ips));
        ips_string[0] = '\0';
        cJSON_ArrayForEach(ip, cur_device_ips) {
            if (ips_string[0] != '\0') {
                strcat(ips_string, ", ");
            }
            strcat(ips_string, ip->valuestring);
        }
    } else {
        ips_string = strdup("");
    }
    return ips_string;
}


