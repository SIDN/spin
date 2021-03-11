#include <errno.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "files.h"

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

    //fprintf(stderr, "[XX] trying file: %s\n", path);
    rc = stat(path, &path_stat);
    if (rc != 0) {
        return NULL;
    }
    if S_ISREG(path_stat.st_mode) {
        fp = fopen(path, "r");
        if (fp != NULL) {
            //fprintf(stderr, "[XX] found!\n");
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

void
try_files2(const char* base_path, const char* path, web_file_t* web_file) {
    char file_path[256];

    snprintf(file_path, 256, "%s%s", base_path, path);
    web_file->fp = try_file(file_path);
    if (web_file->fp != NULL) {
        return;
    }

    snprintf(file_path, 256, "%s%s.gz", base_path, path);
    web_file->fp = try_file(file_path);
    if (web_file->fp != NULL) {
        web_file->gzipped = 1;
        return;
    }

    snprintf(file_path, 256, "%s%s%s", base_path, path, ".html");
    web_file->fp = try_file(file_path);
    if (web_file->fp != NULL) {
        return;
    }

    size_t path_size = strlen(path);
    if (path_size >= 0 && path[strlen(path)-1] == '/') {
        snprintf(file_path, 256, "%s%s%s", base_path, path, "index.html");
        web_file->fp = try_file(file_path);
        if (web_file->fp != NULL) {
            return;
        }
    }
}


/*
 * finds the line that starts with <user>:
 * and places the data in buf.
 * returns 1 if found, 0 if not
 */
static int find_password_line(FILE* file, char* buf, size_t buf_len, const char* user) {
    if (user == NULL || strlen(user) > buf_len) {
        return 0;
    }

    while(getline(&buf, &buf_len, file) >= 0) {
        if (strncmp(user, buf, strlen(user)) == 0) {
            if (strlen(buf) > strlen(user) + 1 && buf[strlen(user)] == ':') {
                return 1;
            }
        }
    }
    return 0;
}

/*
 * finds the pointer to the first byte of the password data
 * (character after first :)
 */
static char* find_password_data_start(const char* password_line) {
    char* c = strchr(password_line, ':');
    if (c != NULL && c-password_line < strlen(password_line)-1) {
        return c + 1;
    }
    return NULL;
}

static int find_password_data_length(const char* password_data_start) {
    char* e = strchr(password_data_start, ':');
    int s = e - password_data_start;
    return s;
}

static int find_password_salt_length(const char* password_data_start) {
    // first should be $, and we are looking for the 3rd $
    char* e = strchr(password_data_start, '$');
    if (e != password_data_start) {
        printf("Error, password data does not start with $\n");
        return -1;
    }
    e = strchr(e+1, '$');
    if (e == NULL) {
        printf("Error, password data does contain second $\n");
        return -1;
    }
    e = strchr(e+1, '$');
    if (e == NULL) {
        printf("Error, password data does contain third $\n");
        return -1;
    }
    
    int s = e - password_data_start;
    return s;
}

int check_password(const char* password_file_name, const char* username, const char* password) {
    size_t BUFLEN = 1024;
    char buf[BUFLEN];
    char passbuf[BUFLEN];
    char saltbuf[BUFLEN];
    
    FILE* password_file = fopen(password_file_name, "r");
    if (password_file == NULL) {
        fprintf(stderr, "Error: unable to read passwords file %s: %s\n", password_file_name, strerror(errno));
        fflush(stdout);
        fflush(stderr);
        return 0;
    }
    
    if (find_password_line(password_file, buf, BUFLEN, username)) {
        char* password_data_start = find_password_data_start(buf);
        if (password_data_start != NULL) {
            int password_data_length = find_password_data_length(password_data_start);
            if (password_data_length > 0) {
                strncpy(passbuf, password_data_start, password_data_length);
            }

            int password_salt_length = find_password_salt_length(password_data_start);
            if (password_salt_length > 0) {
                strncpy(saltbuf, password_data_start, password_salt_length);
            }

            char* crypted = crypt(password, saltbuf);
            if (crypted == NULL) {
                fprintf(stderr, "Unsupported algorithm in password file for user %s\n", username);
            } else if (strncmp(passbuf, crypted, strlen(crypted)) == 0) {
                return 1;
            }
        }
    }
    return 0;
}


long
get_file_size(const char *filename) {
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

char *
read_file(const char *filename) {
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

