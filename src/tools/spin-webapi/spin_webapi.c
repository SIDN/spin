#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <microhttpd.h>

#include "traffic_capture.h"
#include "rpc_client.h"

// TODO: configuration for some of these
#define PORT            1234
#define POSTBUFFERSIZE  512
#define MAXCLIENTS      64
static const char* STATIC_PATH = "/home/jelte/repos/spin/src/web_ui/static";
static const char* TEMPLATE_PATH = "/home/jelte/repos/spin/src/web_ui/templates";

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


const char* busypage =
  "<html><body>This server is busy, please try again later.</body></html>";
const char* completepage =
  "<html><body>The upload has been completed.</body></html>";
const char* errorpage =
  "<html><body>This doesn't seem to be right.</body></html>";
const char* jsonpage =
  "<html><body>No response from JSON-RPC, please try again.</body></html>";
const char* servererrorpage =
  "<html><body>Invalid request.</body></html>";
const char* fileexistspage =
  "<html><body>This file already exists.</body></html>";
const char* fileioerror =
  "<html><body>IO error writing to disk.</body></html>";
const char* const postprocerror =
  "<html><head><title>Error</title></head><body>Error processing POST data</body></html>";
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

    fprintf(stdout, "[XX] try: %s\n", path);
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
 * with '.html', or '/index.html' exists, in that order
 * Returns open file pointer if so, NULL if not
 */
FILE*
try_files(const char* base_path, const char* path) {
    char file_path[256];
    FILE* fp;
    
    snprintf(file_path, 256, "%s%s", base_path, path);
    fp = try_file(file_path);
    if (fp != NULL) {
        fprintf(stdout, "[XX] returning file for : %s\n", file_path);
        return fp;
    }

    snprintf(file_path, 256, "%s%s%s", base_path, path, ".html");
    fprintf(stdout, "[XX] try: %s\n", file_path);
    fp = try_file(file_path);
    if (fp != NULL) {
        fprintf(stdout, "[XX] returning file for : %s\n", file_path);
        return fp;
    }
    
    snprintf(file_path, 256, "%s%s%s", base_path, path, "/index.html");
    fprintf(stdout, "[XX] try: %s\n", file_path);
    fp = try_file(file_path);
    if (fp != NULL) {
        fprintf(stdout, "[XX] returning file for : %s\n", file_path);
        return fp;
    }
    
    fprintf(stdout, "[XX] %s not found\n", file_path);
    return NULL;
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
send_page_from_file(struct MHD_Connection *connection,
                    const char *url) {
    char* page = NULL;
    long file_size = 0;
    FILE* fp = NULL;
    struct MHD_Response* response;
    int ret;

    fp = try_files(STATIC_PATH, url);
    if(fp) {
        fseek(fp, 0, SEEK_END);
        file_size = ftell(fp);
        fseek(fp, 0, SEEK_SET);// needed for next read from beginning of file

        printf("[XX] file size: %ld fp: %p\n", file_size, fp);

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
        return send_page_from_string(connection,
                                     notfounderror,
                                     MHD_HTTP_NOT_FOUND);
    }
    
    return ret;
}

static int
send_page_from_template(struct MHD_Connection *connection,
                        const char *url,
                        ...) {
    char* page = NULL;
    long file_size = 0;
    FILE* fp = NULL;
    struct MHD_Response* response;
    int ret;

    fp = try_files(TEMPLATE_PATH, url);
    if(fp) {
        fseek(fp, 0, SEEK_END);
        file_size = ftell(fp);
        fseek(fp, 0, SEEK_SET);// needed for next read from beginning of file

        printf("[XX] file size: %ld fp: %p\n", file_size, fp);

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

#define TEMPLATE_URL_TCPDUMP "/spin_api/tcpdump"
#define TEMPLATE_URL_TCPDUMP_START "/spin_api/tcpdump_start"
#define TEMPLATE_URL_TCPDUMP_STOP "/spin_api/tcpdump_stop"
#define TEMPLATE_URL_TCPDUMP_STATUS "/spin_api/tcpdump_status"
#define TEMPLATE_URL_CAPTURE "/spin_api/capture"


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
        fprintf(stdout, "[XX] con_cls is NULL\n");
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

    fprintf(stdout, "[XX] have postprocessor\n");

    if (0 == strcasecmp (method, MHD_HTTP_METHOD_GET)) {
        fprintf(stdout, "[XX] GET URL: %s\n", url);
        // The API endpoints only accept POST for now
        if (strncmp(url, "/spin_api/jsonrpc", 18) == 0 || strncmp(url, "/spin_api/jsonrpc/", 19) == 0) {
            return send_page_from_string(connection,
                                         methodnotallowederror,
                                         MHD_HTTP_METHOD_NOT_ALLOWED);
        } else if (strncmp(url, TEMPLATE_URL_TCPDUMP_START, strlen(TEMPLATE_URL_TCPDUMP_START)) == 0) {
            printf("[XX] START TCPDUMP\n");
            return tc_answer_direct_capture_request(connection, url);
        } else if (strncmp(url, TEMPLATE_URL_TCPDUMP_STOP, strlen(TEMPLATE_URL_TCPDUMP_STOP)) == 0) {
            const char* device_mac = MHD_lookup_connection_value (connection, MHD_GET_ARGUMENT_KIND, "device");
            printf("[XX] STOP TCPDUMP FOR %s\n", device_mac);
            tc_stop_capture_for(device_mac);
            // Return an empty ok answer?
            return send_page_from_string(connection,
                                         "",
                                         MHD_HTTP_OK);
        } else if (strncmp(url, TEMPLATE_URL_CAPTURE, strlen(TEMPLATE_URL_CAPTURE)) == 0) {
            /* template parameters:
             * device name
             * device mac
             * ip address(es)
             */
            const char* device_mac = MHD_lookup_connection_value (connection, MHD_GET_ARGUMENT_KIND, "device");
            return send_page_from_template(connection, url + 9, "1", device_mac, "3", NULL);
        } else if (strncmp(url, TEMPLATE_URL_TCPDUMP, strlen(TEMPLATE_URL_TCPDUMP) + 1) == 0) {
            // Template args:
            // device mac, running, bytes_sent
            // get 1 from 'dev' parameter in query
            // need to get 2 and 3 from our tcpdump manager code
            const char* device_param = MHD_lookup_connection_value (connection, MHD_GET_ARGUMENT_KIND, "device");
            printf("[XX] QUERY PARAMETER: %s\n", device_param);
            return send_page_from_template(connection, url + 9, device_param, "B", NULL);
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
            return send_page_from_template(connection, url + 9, device_mac, running, bytes_string, NULL);
        } else if (strncmp(url, "/spin_api/", 10) == 0) {
            // strip the 'spin_api' part from the url
            //return send_page_from_file(connection, url);
            return send_page_from_template(connection, url + 9, "GENERAL", NULL);
        }
        return send_page_from_file(connection, url);
    }

    if (0 == strcasecmp (method, MHD_HTTP_METHOD_POST)) {
        fprintf(stdout, "[XX] POST URL: %s\n", url);
        struct connection_info_struct *con_info = *con_cls;
        fprintf(stdout, "[XX] data size: %ld\n", *upload_data_size);

        if (0 != *upload_data_size) {
            fprintf(stdout, "[XX] upload data size not 0\n");
            /* Upload not yet done */
            if (0 != con_info->answercode) {
                /* we already know the answer, skip rest of upload */
                fprintf(stdout, "[XX] done processing, send answer\n");
                *upload_data_size = 0;
                return MHD_YES;
            }
            // TODO: check if content_type application/json
            char* json_response = send_jsonrpc_message(upload_data);
            if (json_response != NULL) {
                con_info->dynamic_answerstring = json_response;
                con_info->answercode = MHD_HTTP_OK;
            } else {
                fprintf(stdout, "[XX] JSON response null\n");
                con_info->answerstring = errorpage;
                con_info->answercode = MHD_HTTP_OK;
            }
            *upload_data_size = 0;

            return MHD_YES;
        }

        fprintf(stdout, "[XX] upload data size is 0, upload is done\n");

        /* Upload finished */
        if (0 == con_info->answercode) {
            fprintf(stdout, "[XX] answercode is 0, set success\n");
            /* No errors encountered, declare success */
            con_info->answerstring = completepage;
            con_info->answercode = MHD_HTTP_OK;
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


int
main() {
    struct MHD_Daemon *daemon;
    int terminal_input = 0;

    daemon = MHD_start_daemon(MHD_USE_THREAD_PER_CONNECTION,
                              PORT, NULL, NULL,
                              &answer_to_connection, NULL,
                              MHD_OPTION_NOTIFY_COMPLETED, &request_completed, NULL,
                              MHD_OPTION_END);
    if (NULL == daemon) {
        fprintf (stderr,
                 "Failed to start daemon\n");
        return 1;
    }
    while (1) {
        terminal_input = getchar();
        if (terminal_input == 'q') {
            // Stop all running capture processes, and wait for them to end
            tc_stop_all_captures();
            while (tc_captures_running() > 0) {
                //printf("[XX] Captures running: %d\n", tc_captures_running());
            }
            MHD_stop_daemon(daemon);
            return 0;
        }
    }
}
