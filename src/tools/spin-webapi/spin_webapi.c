#include <microhttpd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/*
 * TODO:
 * - serve static pages
 * - serve JSON-rpc pages
 * - configuration (IP, port, page location?)
 * - documentation, defaults, etc.
 * 
 * Optional:
 * - redo old tcpdump?
 * 
 * Alternative:
 * - call spin_webui.lua instead of full remake?
 */

#define PAGE "<html><head><title>libmicrohttpd demo</title>"\
             "</head><body>Hello, world!</body></html>"

#define NOT_FOUND "<html><head><title>SPIN Web API</title>"\
             "</head><body>File not found</body></html>"

/* 
 * Almost painfully minimalistic template 'engine'
 * replaces all occurences of %TEMPLATE_ARGX% (X being a number)
 * with the corresponding variable argument
 *
 * Note: returned data pointer must be freed by caller
 */
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

/*
 * Tries whether the file path exists, and if not, whether the path
 * with '.html', or '/index.html' exists, in that order
 * Returns open file pointer if so, NULL if not
 */
FILE*
try_file(const char* base_path, const char* path) {
    char file_path[256];
    FILE* fp;
    
    snprintf(file_path, 256, "%s%s", base_path, path);
    fprintf(stdout, "[XX] try: %s\n", file_path);
    fp = fopen(file_path, "r");
    if (fp != NULL) {
        fprintf(stdout, "[XX] returning file for : %s\n", file_path);
        return fp;
    }

    snprintf(file_path, 256, "%s%s%s", base_path, path, ".html");
    fprintf(stdout, "[XX] try: %s\n", file_path);
    fp = fopen(file_path, "r");
    if (fp != NULL) {
        fprintf(stdout, "[XX] returning file for : %s\n", file_path);
        return fp;
    }
    
    snprintf(file_path, 256, "%s%s%s", base_path, path, "/index.html");
    fprintf(stdout, "[XX] try: %s\n", file_path);
    fp = fopen(file_path, "r");
    if (fp != NULL) {
        fprintf(stdout, "[XX] returning file for : %s\n", file_path);
        return fp;
    }
    
    fprintf(stdout, "[XX] %s not found\n", file_path);
    return NULL;
}

int
serve_static_page(void * cls,
         struct MHD_Connection * connection,
         const char * url,
         const char * method,
         const char * version,
         const char * upload_data,
         size_t * upload_data_size,
         void ** ptr) {

    static int dummy;
    struct MHD_Response * response;
    int ret;

    char* page = NULL;
    long file_size = 0;
    FILE* fp = NULL;

    if (0 != strcmp(method, "GET")) {
        return MHD_NO; /* unexpected method */
    }
    if (&dummy != *ptr) {
        /* The first time only the headers are valid,
         do not respond in the first round... */
        *ptr = &dummy;
        return MHD_YES;
    }
    if (0 != *upload_data_size) {
        return MHD_NO; /* upload data in a GET!? */
    }
    *ptr = NULL; /* clear context pointer */

    /* Read the file, if any */
    fprintf(stdout, "Requested URL: %s\n", url);
    
    fp = try_file("/home/jelte/repos/spin/src/web_ui/static", url);
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
        page = NOT_FOUND;
        response = MHD_create_response_from_buffer (strlen(page),
                                                    (void*) page,
                                                    MHD_RESPMEM_PERSISTENT);
        ret = MHD_queue_response(connection,
                                 MHD_HTTP_NOT_FOUND,
                                 response);
        MHD_destroy_response(response);
    }
    
    return ret;
}

int
serve_api_call(void * cls,
               struct MHD_Connection * connection,
               const char * url,
               const char * method,
               const char * version,
               const char * upload_data,
               size_t * upload_data_size,
               void ** ptr) {

    /* Depending on the specific call, we may need to respond differently;
     * some are simply static pages as well */
    static int dummy;
    struct MHD_Response * response;
    int ret;

    char* page = NULL;
    long file_size = 0;
    FILE* fp = NULL;

    if (0 != strcmp(method, "GET")) {
        return MHD_NO; /* unexpected method */
    }
    if (&dummy != *ptr) {
        /* The first time only the headers are valid,
         do not respond in the first round... */
        *ptr = &dummy;
        return MHD_YES;
    }
    if (0 != *upload_data_size) {
        return MHD_NO; /* upload data in a GET!? */
    }
    *ptr = NULL; /* clear context pointer */

    /* Read the file, if any */
    fprintf(stdout, "Requested API URL: %s\n", url);
    
    fp = try_file("/home/jelte/repos/spin/src/web_ui/templates", url);
    if(fp) {
        fseek(fp, 0, SEEK_END);
        file_size = ftell(fp);
        fseek(fp, 0, SEEK_SET);// needed for next read from beginning of file

        printf("[XX] file size: %ld fp: %p\n", file_size, fp);

        // page will be freed by MHD (MHD_RESPMEM_MUST_FREE)
        page = malloc(file_size);
        if (page == NULL) {
            printf("OHNOES!\n");
            printf("OHNOES!\n");
            printf("OHNOES!\n");
            printf("OHNOES!\n");
        }
        memset(page, 0, file_size);
        fread(page, file_size, 1, fp);
    
        fclose(fp);
        
        char* templres = template_render(page, file_size, "FOOBAR", "EXISTATIONASDFASDF", "AAAAA", NULL);
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
        page = NOT_FOUND;
        response = MHD_create_response_from_buffer (strlen(page),
                                                    (void*) page,
                                                    MHD_RESPMEM_PERSISTENT);
        ret = MHD_queue_response(connection,
                                 MHD_HTTP_NOT_FOUND,
                                 response);
        MHD_destroy_response(response);
    }
    
    return ret;
}

int
handle_request(void * cls,
               struct MHD_Connection * connection,
               const char * url,
               const char * method,
               const char * version,
               const char * upload_data,
               size_t * upload_data_size,
               void ** ptr) {
    
    // Finally, try to serve a static page
    printf("[XX] URL: '%s'\n", url);
    if (strncmp(url, "/spin_api/", 10) == 0) {
        printf("[XX] api call\n");
        return serve_api_call(cls, connection, url+9, method, version, upload_data, upload_data_size, ptr);
    }
    
    printf("[XX] static call\n");
    return serve_static_page(cls, connection, url, method, version, upload_data, upload_data_size, ptr);
}

int main(int argc, char ** argv) {
    struct MHD_Daemon * d;
    if (argc != 2) {
        printf("%s PORT\n", argv[0]);
        return 1;
    }
    d = MHD_start_daemon(MHD_USE_THREAD_PER_CONNECTION,
                         atoi(argv[1]),
                         NULL,
                         NULL,
                         &handle_request,
                         PAGE,
                         MHD_OPTION_END);
    if (d == NULL) {
        return 1;
    }
    (void) getc (stdin);
    MHD_stop_daemon(d);
    return 0;
}