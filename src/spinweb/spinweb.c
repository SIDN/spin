#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <microhttpd.h>

#include "config.h"
#include "traffic_capture.h"
#include "rpc_client.h"
#include "spin_config.h"
#include "version.h"

#define POSTBUFFERSIZE  512
#define MAXCLIENTS      64

#define TEMPLATE_SRC_PATH SRCDIR "/templates"
#define TEMPLATE_INSTALL_PATH DATADIR "/spin/spinweb/templates"
#define STATIC_SRC_PATH SRCDIR "/static"
#define STATIC_INSTALL_PATH DATADIR "/spin/spinweb/static"

//
// Endpoints
//

// main capture page for a device
#define TEMPLATE_URL_CAPTURE "/spin_api/capture"
// 'old style' capture
#define TEMPLATE_URL_TCPDUMP "/spin_api/tcpdump"
#define TEMPLATE_URL_TCPDUMP_START "/spin_api/tcpdump_start"
#define TEMPLATE_URL_TCPDUMP_STOP "/spin_api/tcpdump_stop"
#define TEMPLATE_URL_TCPDUMP_STATUS "/spin_api/tcpdump_status"
// 'new style' capture
#define TEMPLATE_URL_MQTT_CAPTURE_START "/spin_api/capture_start"
#define TEMPLATE_URL_MQTT_CAPTURE_STOP "/spin_api/capture_stop"


enum ConnectionType {
    GET = 0,
    POST = 1
};


/**
 * Information we keep per connection.
 */
struct connection_info_struct {
    enum ConnectionType connectiontype;

    /**
    * HTTP response body we will return, for static data string
    * NULL if not yet known.
    */
    const char* answerstring;

    /**
    * HTTP response body we will return, for dynamically allocated data string
    * NULL if not yet known.
    * Will be freed when the request has been answered.
    */
    char* dynamic_answerstring;

    /**
    * HTTP status code we will return, 0 for undecided.
    */
    unsigned int answercode;
};


const char* errorpage =
  "<html><body>This doesn't seem to be right.</body></html>";
const char* rpcerrorpage =
  "<html><body>Error trying to contact RPC server on spinweb host.</body></html>";
const char* jsonpage =
  "<html><body>No response from JSON-RPC, please try again.</body></html>";
const char* notfounderror =
  "<html><body><title>Error</title></head><body>File not found.</body></html>";
const char* methodnotallowederror =
  "<html><body><title>Error</title></head><body>HTTP Method not allowed for this URL.</body></html>";

/*
 * Returns opened FILE* pointer if the path exists and is a regular
 * file.
 * NULL otherwise, or if fopen() fails.
 */
FILE*
try_file(const char* path) {
    struct stat path_stat;
    FILE* fp;
    int rc;

    rc = stat(path, &path_stat);
    if (rc != 0) {
        return NULL;
    }
    if S_ISREG(path_stat.st_mode) {
        fp = fopen(path, "r");
        if (fp != NULL) {
            return fp;
        }
    }
    return NULL;
}


/*
 * Tries whether the file path exists, and if not, whether the path
 * with '.html', or (if url ends with /) 'index.html' exists, in that order
 * Returns open file pointer if so, NULL if not
 */
FILE*
try_files(const char* base_path, const char* path) {
    char file_path[256];
    FILE* fp;

    snprintf(file_path, 256, "%s%s", base_path, path);
    fp = try_file(file_path);
    if (fp != NULL) {
        return fp;
    }

    snprintf(file_path, 256, "%s%s%s", base_path, path, ".html");
    fp = try_file(file_path);
    if (fp != NULL) {
        return fp;
    }

    size_t path_size = strlen(path);
    if (path_size >= 0 && path[strlen(path)-1] == '/') {
        snprintf(file_path, 256, "%s%s%s", base_path, path, "index.html");
        fp = try_file(file_path);
        if (fp != NULL) {
            return fp;
        }
    }

    return NULL;
}

/*
 * Tries to find a template file for the given path
 * First checks the 'local' source directory (in case we are
 * running from a source build), then the global directory
 */
FILE* try_template_files(const char* path) {
    FILE* fp;
    fp = try_files(TEMPLATE_SRC_PATH, path);
    if (fp == NULL) {
        fp = try_files(TEMPLATE_INSTALL_PATH, path);
    }
    return fp;
}

/*
 * Tries to find a static file for the given path
 * First checks the 'local' source directory (in case we are
 * running from a source build), then the global directory
 */
FILE* try_static_files(const char* path) {
    FILE* fp;
    fp = try_files(STATIC_SRC_PATH, path);
    if (fp == NULL) {
        fp = try_files(STATIC_INSTALL_PATH, path);
    }
    return fp;
}


#define TEMPLATE_ARG_STR_SIZE 64

char* template_render(const char* template, size_t template_size, ...) {
    // Keep track of size in case replacement is much larger than
    // reserved space
    size_t MAX_SIZE = template_size * 2;
    size_t cur_size = 0;
    char* new_template = malloc(MAX_SIZE);

    // variable arguments
    const char* template_var = "";
    va_list valist;
    int i = 0;
    char templ_arg_str[TEMPLATE_ARG_STR_SIZE];

    // tracking data when replacing variables
    char* pos;

    memset(new_template, 0, MAX_SIZE);
    memcpy(new_template, template, template_size);
    new_template[template_size] = '\0';

    va_start(valist, template_size);

    cur_size = template_size;
    while (template_var != NULL) {
        template_var = va_arg(valist, const char*);
        if (template_var != NULL) {
            snprintf(templ_arg_str, TEMPLATE_ARG_STR_SIZE, "_TEMPLATE_ARG%d_", i);
            pos = new_template;
            while (pos != NULL) {
                pos = strstr(pos, templ_arg_str);
                if (pos != NULL) {
                    cur_size = cur_size - strlen(templ_arg_str) + strlen(template_var);
                    if (cur_size > MAX_SIZE - 1) {
                        MAX_SIZE = MAX_SIZE * 2;
                        new_template = realloc(new_template, MAX_SIZE);
                    }

                    // backup the rest, replace the macro, and add
                    // the rest again.
                    char* buf = strdup(pos + strlen(templ_arg_str));
                    strcpy(pos, template_var);
                    strcpy(pos + strlen(template_var), buf);
                    free(buf);
                }
            }
            i++;
        }
    }

    va_end(valist);

    return new_template;
}

char* template_render2(const char* template, size_t template_size, va_list valist) {
    // Keep track of size in case replacement is much larger than
    // reserved space
    size_t MAX_SIZE = template_size * 2;
    size_t cur_size = 0;
    char* new_template = malloc(MAX_SIZE);

    // variable arguments
    const char* template_var = "";
    int i = 0;
    char templ_arg_str[TEMPLATE_ARG_STR_SIZE];

    // tracking data when replacing variables
    char* pos;

    memset(new_template, 0, MAX_SIZE);
    memcpy(new_template, template, template_size);
    new_template[template_size] = '\0';

    cur_size = template_size;
    while (template_var != NULL) {
        template_var = va_arg(valist, const char*);
        if (template_var != NULL) {
            snprintf(templ_arg_str, TEMPLATE_ARG_STR_SIZE, "_TEMPLATE_ARG%d_", i);
            pos = new_template;
            while (pos != NULL) {
                pos = strstr(pos, templ_arg_str);
                if (pos != NULL) {
                    cur_size = cur_size - strlen(templ_arg_str) + strlen(template_var);
                    if (cur_size > MAX_SIZE - 1) {
                        MAX_SIZE = MAX_SIZE * 2;
                        new_template = realloc(new_template, MAX_SIZE);
                    }

                    // backup the rest, replace the macro, and add
                    // the rest again.
                    char* buf = strdup(pos + strlen(templ_arg_str));
                    strcpy(pos, template_var);
                    strcpy(pos + strlen(template_var), buf);
                    free(buf);
                }
            }
            i++;
        }
    }

    return new_template;
}


static int
send_page_from_string(struct MHD_Connection *connection,
                      const char *page,
                      int status_code) {
    int ret;
    struct MHD_Response* response;

    response = MHD_create_response_from_buffer (strlen (page),
                                                (void *) page,
                                                MHD_RESPMEM_MUST_COPY);
    if (!response) {
        return MHD_NO;
    }
    MHD_add_response_header(response,
                            MHD_HTTP_HEADER_CONTENT_TYPE,
                            "text/html");
    ret = MHD_queue_response(connection,
                             status_code,
                             response);
    MHD_destroy_response(response);

    return ret;
}

static int
send_redirect_add_slash(struct MHD_Connection *connection,
                        int status_code) {
    int ret;
    struct MHD_Response* response;

    response = MHD_create_response_from_buffer (0, (void *) "",
                                                MHD_RESPMEM_PERSISTENT);
    if (!response) {
        return MHD_NO;
    }
    MHD_add_response_header (response, "Location", "./");
    ret = MHD_queue_response(connection,
                             status_code,
                             response);
    MHD_destroy_response(response);

    return ret;
}


static int
send_page_from_file(struct MHD_Connection *connection,
                    const char *url) {
    char* page = NULL;
    long file_size = 0;
    FILE* fp = NULL;
    struct MHD_Response* response;
    int ret;

    fp = try_static_files(url);
    if(fp) {
        fseek(fp, 0, SEEK_END);
        file_size = ftell(fp);
        fseek(fp, 0, SEEK_SET);// needed for next read from beginning of file

        // page will be freed by MHD (MHD_RESPMEM_MUST_FREE)
        page = malloc(file_size);
        memset(page, 0, file_size);
        fread(page, file_size, 1, fp);

        fclose(fp);

        response = MHD_create_response_from_buffer (file_size,
                                                    (void*) page,
                                                    MHD_RESPMEM_MUST_FREE);
        ret = MHD_queue_response(connection,
                                 MHD_HTTP_OK,
                                 response);
        MHD_destroy_response(response);
    } else {
        // If the URL does not end with a /, redirect to one
        // If the URL does, send a 404
        size_t url_len = strlen(url);
        if (url_len > 0 && url[url_len - 1] == '/') {
            return send_page_from_string(connection,
                                         notfounderror,
                                         MHD_HTTP_NOT_FOUND);
        } else {
            return send_redirect_add_slash(connection, 302);
            //MHD_add_response_header (response, "Location", "http://somesite.com/");
            //return send_page_from_string(connection,
            //                             notfounderror,
            //                             MHD_HTTP_NOT_FOUND);
        }
    }

    return ret;
}

static int
send_page_from_template(struct MHD_Connection *connection,
                        FILE* fp,
                        const char *url,
                        ...) {
    char* page = NULL;
    long file_size = 0;
    struct MHD_Response* response;
    int ret;

    if(fp) {
        fseek(fp, 0, SEEK_END);
        file_size = ftell(fp);
        fseek(fp, 0, SEEK_SET);// needed for next read from beginning of file

        // page will be freed by MHD (MHD_RESPMEM_MUST_FREE)
        page = malloc(file_size);
        if (page == NULL) {
            // TODO: handle oom
            fprintf(stderr, "out of memory\n");
        }
        memset(page, 0, file_size);
        fread(page, file_size, 1, fp);

        fclose(fp);

        //char* templres = template_render(page, file_size, "FOOBAR", "EXISTATIONASDFASDF", "AAAAA", NULL);
        va_list valist;
        va_start(valist, url);
        char* templres = template_render2(page, file_size, valist);
        va_end(valist);
        file_size = strlen(templres);
        free(page);
        page = templres;

        response = MHD_create_response_from_buffer (file_size,
                                                    (void*) page,
                                                    MHD_RESPMEM_MUST_FREE);
        ret = MHD_queue_response(connection,
                                 MHD_HTTP_OK,
                                 response);
        MHD_destroy_response(response);
    } else {
        return send_page_from_string(connection,
                                     notfounderror,
                                     MHD_HTTP_NOT_FOUND);
    }

    return ret;
}


static void
request_completed(void *cls,
                  struct MHD_Connection *connection,
                  void **con_cls,
                  enum MHD_RequestTerminationCode toe)
{
    struct connection_info_struct *con_info = *con_cls;
    (void)cls;         /* Unused. Silent compiler warning. */
    (void)connection;  /* Unused. Silent compiler warning. */
    (void)toe;         /* Unused. Silent compiler warning. */

    if (NULL == con_info) {
        return;
    }

    if (con_info->connectiontype == POST) {
        if (con_info->dynamic_answerstring != NULL) {
            free(con_info->dynamic_answerstring);
        }
    }

    free(con_info);
    *con_cls = NULL;
}

static int
answer_to_connection(void *cls,
                     struct MHD_Connection *connection,
                     const char *url,
                     const char *method,
                     const char *version,
                     const char *upload_data,
                     size_t *upload_data_size,
                     void **con_cls) {
    (void)cls;               /* Unused. Silent compiler warning. */
    (void)version;           /* Unused. Silent compiler warning. */

    if (NULL == *con_cls) {
        /* First call, setup data structures */
        struct connection_info_struct *con_info;

        con_info = malloc(sizeof (struct connection_info_struct));
        if (NULL == con_info) {
            return MHD_NO;
        }
        con_info->answercode = 0; /* none yet */
        con_info->answerstring = NULL;
        con_info->dynamic_answerstring = NULL;

        if (0 == strcasecmp (method, MHD_HTTP_METHOD_POST)) {
            con_info->connectiontype = POST;
        } else {
            con_info->connectiontype = GET;
        }

        *con_cls = (void *)con_info;

        return MHD_YES;
    }

    if (0 == strcasecmp (method, MHD_HTTP_METHOD_GET)) {
        // The API endpoints only accept POST for now
        if (strncmp(url, "/spin_api/jsonrpc", 18) == 0 || strncmp(url, "/spin_api/jsonrpc/", 19) == 0) {
            return send_page_from_string(connection,
                                         methodnotallowederror,
                                         MHD_HTTP_METHOD_NOT_ALLOWED);
        } else if (strncmp(url, TEMPLATE_URL_MQTT_CAPTURE_START, strlen(TEMPLATE_URL_MQTT_CAPTURE_START)) == 0) {
            const char* device_mac = MHD_lookup_connection_value (connection, MHD_GET_ARGUMENT_KIND, "device");
            int result = tc_start_mqtt_capture_for(device_mac);
            if (result == 0) {
                // Return an empty ok answer?
                return send_page_from_string(connection,
                                             "",
                                             MHD_HTTP_OK);
            } else {
                return send_page_from_string(connection,
                                             "error",
                                             MHD_HTTP_INTERNAL_SERVER_ERROR);
            }
        } else if (strncmp(url, TEMPLATE_URL_MQTT_CAPTURE_STOP, strlen(TEMPLATE_URL_MQTT_CAPTURE_STOP)) == 0) {
            const char* device_mac = MHD_lookup_connection_value (connection, MHD_GET_ARGUMENT_KIND, "device");
            tc_stop_capture_for(device_mac);
            // Return an empty ok answer?
            return send_page_from_string(connection,
                                         "",
                                         MHD_HTTP_OK);
        } else if (strncmp(url, TEMPLATE_URL_TCPDUMP_START, strlen(TEMPLATE_URL_TCPDUMP_START)) == 0) {
            return tc_answer_direct_capture_request(connection, url);
        } else if (strncmp(url, TEMPLATE_URL_TCPDUMP_STOP, strlen(TEMPLATE_URL_TCPDUMP_STOP)) == 0) {
            const char* device_mac = MHD_lookup_connection_value (connection, MHD_GET_ARGUMENT_KIND, "device");
            tc_stop_capture_for(device_mac);
            // Return an empty ok answer?
            return send_page_from_string(connection,
                                         "",
                                         MHD_HTTP_OK);
        } else if (strncmp(url, TEMPLATE_URL_CAPTURE, strlen(TEMPLATE_URL_CAPTURE)) == 0) {
            /* template parameters:
             * TODO
             * device name
             * device mac
             * ip address(es)
             */
            spin_data devlist = rpcc_list_devices();
            spin_data_delete(devlist);
            const char* device_mac = MHD_lookup_connection_value (connection, MHD_GET_ARGUMENT_KIND, "device");
            spin_data device = rpcc_get_device_by_mac(device_mac);
            char* device_name = rpcc_get_device_name(device);
            char* device_ips = rpcc_get_device_ips_as_string(device);
            FILE* template_fp = try_template_files(url+9);

            int result = send_page_from_template(connection, template_fp, url + 9, device_name, device_mac, device_ips, NULL);

            free(device_name);
            free(device_ips);
            spin_data_delete(device);

            return result;
        } else if (strncmp(url, TEMPLATE_URL_TCPDUMP, strlen(TEMPLATE_URL_TCPDUMP) + 1) == 0) {
            // Template args:
            // device mac, running, bytes_sent
            // get 1 from 'dev' parameter in query
            // need to get 2 and 3 from our tcpdump manager code
            const char* device_param = MHD_lookup_connection_value (connection, MHD_GET_ARGUMENT_KIND, "device");
            FILE* template_fp = try_template_files(url+9);
            return send_page_from_template(connection, template_fp, url + 9, device_param, "B", NULL);
        } else if (strncmp(url, TEMPLATE_URL_TCPDUMP_STATUS, strlen(TEMPLATE_URL_TCPDUMP_STATUS) + 1) == 0) {
            // Template args:
            // device mac, running, bytes_sent
            // get 1 from 'dev' parameter in query
            // need to get 2 and 3 from our tcpdump manager code
            const char* device_mac = MHD_lookup_connection_value (connection, MHD_GET_ARGUMENT_KIND, "device");
            const char* running = tc_capture_running_for(device_mac)?"true":"false";
            char bytes_string[100];
            int bytes_sent = tc_get_bytes_sent_for(device_mac);
            snprintf(bytes_string, 100, "%d", bytes_sent);
            FILE* template_fp = try_template_files(url+9);
            return send_page_from_template(connection, template_fp, url + 9, device_mac, running, bytes_string, NULL);
        //} else if (strncmp(url, "/spin_api/", 10) == 0) {
            // strip the 'spin_api' part from the url
            //return send_page_from_file(connection, url);
        //    return send_page_from_template(connection, url + 9, "GENERAL", NULL);
        }
        // Try any template file
        FILE* template_fp = try_template_files(url+9);
        if (template_fp != NULL) {
            return send_page_from_template(connection, template_fp, url + 9, "GENERAL", NULL);
        }

        // Try any static file
        return send_page_from_file(connection, url);
    }

    if (0 == strcasecmp (method, MHD_HTTP_METHOD_POST)) {
        struct connection_info_struct *con_info = *con_cls;

        if (0 != *upload_data_size) {
            /* Upload not yet done */
            if (0 != con_info->answercode) {
                /* we already know the answer, skip rest of upload */
                *upload_data_size = 0;
                return MHD_YES;
            }
            // TODO: check if content_type application/json
            char* json_response = send_rpc_message_raw(upload_data);
            if (json_response != NULL) {
                con_info->dynamic_answerstring = json_response;
                con_info->answercode = MHD_HTTP_OK;
            } else {
                con_info->answerstring = rpcerrorpage;
                con_info->answercode = MHD_HTTP_OK;
            }
            *upload_data_size = 0;

            return MHD_YES;
        }

        if (con_info->answerstring) {
            return send_page_from_string(connection,
                                         con_info->answerstring,
                                         con_info->answercode);
        } else if (con_info->dynamic_answerstring) {
            return send_page_from_string(connection,
                                         con_info->dynamic_answerstring,
                                         con_info->answercode);
        }
    }

    /* Not a GET or a POST, generate error */
    return send_page_from_string(connection,
                                 errorpage,
                                 MHD_HTTP_BAD_REQUEST);
}

#define MAX_DAEMONS 10

static long
get_file_size (const char *filename) {
    FILE *fp;

    fp = fopen(filename, "rb");
    if (fp) {
        long size;

        if ((0 != fseek(fp, 0, SEEK_END)) || (-1 == (size = ftell (fp)))) {
            size = 0;
        }

        fclose (fp);

        return size;
    } else {
        return 0;
    }
}

static char *
read_file (const char *filename) {
    FILE *fp;
    char *buffer;
    long size;

    size = get_file_size(filename);
    if (0 == size) {
        return NULL;
    }

    fp = fopen (filename, "rb");
    if (!fp) {
        return NULL;
    }

    buffer = malloc (size + 1);
    if (! buffer) {
        fclose (fp);
        return NULL;
    }
    buffer[size] = '\0';

    if (size != (long) fread (buffer, 1, size, fp)) {
        free (buffer);
        buffer = NULL;
    }

    fclose (fp);
    return buffer;
}

int
start_daemon(char* address, int port, char* tls_cert_pem, char* tls_key_pem, struct MHD_Daemon* daemons[], int daemon_count) {
    struct sockaddr_in addr1;
    addr1.sin_family = AF_INET;
    addr1.sin_port = htons(port);
    printf("Binding to '%s' port %d\n", address, port);
    if (inet_aton(address, &addr1.sin_addr) == 0) {
        fprintf(stderr, "Invalid IP address: %s\n", address);
        return 1;
    }

    unsigned int daemon_flags = MHD_USE_THREAD_PER_CONNECTION | MHD_USE_INTERNAL_POLLING_THREAD;
    if (tls_key_pem != NULL && tls_cert_pem != NULL) {
        daemon_flags = daemon_flags | MHD_USE_SSL;
    }

    daemons[daemon_count] = MHD_start_daemon(daemon_flags,
                              port, NULL, NULL,
                              &answer_to_connection, NULL,
                              MHD_OPTION_NOTIFY_COMPLETED, &request_completed, NULL,
                              MHD_OPTION_SOCK_ADDR, &addr1,
                              MHD_OPTION_HTTPS_MEM_KEY, tls_key_pem,
                              MHD_OPTION_HTTPS_MEM_CERT, tls_cert_pem,
                              MHD_OPTION_END);
    if (NULL == daemons[daemon_count]) {
        fprintf (stderr,
                 "Failed to start daemon: %s\n", strerror(errno));
        return 1;
    }
    return 0;
}

int
start_daemons(const char* address_list, int port, char* tls_cert_pem, char* tls_key_pem, struct MHD_Daemon* daemons[], int* daemon_count) {
    // address_list is a comma-separated list of IP addresses
    // it may contain whitespace. Clone it so we can safely use strtok
    char* dup = strdup(address_list);
    char* p = strtok (dup,",");
    while (p != NULL) {
        while (*p == ' ') {
            p++;
        }
        printf ("start daemon at '%s'\n",p);
        if (start_daemon(p, port, tls_cert_pem, tls_key_pem, daemons, *daemon_count) != 0) {
            free(dup);
            return 1;
        }
        *daemon_count = *daemon_count + 1;
        p = strtok (NULL, ",");
    }
    free(dup);
    return 0;
}

void print_help() {
    printf("Usage: spinweb [options]\n");
    printf("Options:\n");
    printf("-c <file>\t\tspecify spin config file (default: %s)\n", CONFIG_FILE);
    printf("-i <IP address>\t\tlisten on the given IP address (default: 127.0.0.1)\n");
    printf("-p <port number>\t\tlisten on the given port number (default: 13026)\n");
    printf("-v\t\t\tprint the version of spinweb and exit\n");
}

void print_version() {
    printf("SPIN web version %s\n", BUILD_VERSION);
    printf("Build date: %s\n", BUILD_DATE);
}

int
main(int argc, char** argv) {
    /* It would appear MHD can only accept one address,
     * so in order to allow multiple addresses to be specified, without
     * going 'all' immediately, we may need multiple daemon instances
     */
    struct MHD_Daemon *daemons[MAX_DAEMONS];
    int daemon_count = 0;
    // when started in a terminal, press 'q<enter>' to quit gracefully
    int terminal_input = 0;

    char* config_file = NULL;
    int c;

    char* interfaces = NULL;
    int port_number = 0;

    char* tls_cert_file = NULL;
    char* tls_cert_pem = NULL;
    char* tls_key_file = NULL;
    char* tls_key_pem = NULL;

    while ((c = getopt (argc, argv, "c:hi:p:v")) != -1) {
        switch (c) {
        case 'c':
            config_file = optarg;
            break;
        case 'h':
            print_help();
            exit(0);
            break;
        case 'i':
            if (interfaces == NULL) {
                interfaces = strdup(optarg);
            } else {
                char* tmp = malloc(strlen(interfaces) + strlen(optarg) + 3);
                strcpy(tmp, interfaces);
                strcat(tmp, ", ");
                strcat(tmp, optarg);
                free(interfaces);
                interfaces = tmp;
            }
            break;
        case 'p':
            port_number = atoi(optarg);
            break;
        case 'v':
            print_version();
            exit(0);
            break;
        }
    }

    if (config_file) {
        init_config(config_file, 1);
    } else {
        // Don't error if default config file doesn't exist
        init_config(CONFIG_FILE, 0);
    }

    if (interfaces == NULL) {
        interfaces = strdup(spinconfig_spinweb_interfaces());
    }
    if (port_number == 0) {
        port_number = spinconfig_spinweb_port();
    }

    tls_cert_file = spinconfig_spinweb_tls_certificate_file();
    tls_key_file = spinconfig_spinweb_tls_key_file();

    if (tls_cert_file != NULL && strlen(tls_cert_file) > 0) {
        tls_cert_pem = read_file(tls_cert_file);
        if (tls_cert_pem == NULL) {
            fprintf(stderr, "Error reading TLS certificate file %s: %s\n", tls_cert_file, strerror(errno));
            return errno;
        }
        if (tls_key_file == NULL || strlen(tls_key_file) == 0) {
            fprintf(stderr, "Error: TLS certificate file given, but no TLS key file\n");
            return 1;
        }
    }

    if (tls_key_file != NULL && strlen(tls_key_file) > 0) {
        tls_key_pem = read_file(tls_key_file);
        if (tls_key_pem == NULL) {
            fprintf(stderr, "Error reading TLS key file %s: %s\n", tls_key_file, strerror(errno));
            return errno;
        }
        if (tls_cert_file == NULL || strlen(tls_cert_file) == 0) {
            fprintf(stderr, "Error: TLS key file given, but no TLS certificate file\n");
            return 1;
        }
    }

    if (start_daemons(interfaces, port_number, tls_cert_pem, tls_key_pem, daemons, &daemon_count) != 0) {
        free(tls_cert_pem);
        free(tls_key_pem);
        free(interfaces);
        return errno;
    }

    while (1) {
        terminal_input = getchar();
        if (terminal_input == 'q') {
            // Stop all running capture processes, and wait for them to end
            tc_stop_all_captures();
            while (tc_captures_running() > 0) {
                sleep(1);
            }
            for (int i=0; i<daemon_count; i++) {
                MHD_stop_daemon(daemons[i]);
            }
            free(tls_cert_pem);
            free(tls_key_pem);
            free(interfaces);
            return 0;
        } else if (terminal_input == EOF) {
            // running in background, just sleep forever
            select(0, NULL, NULL, NULL, NULL);
        }
    }
}
