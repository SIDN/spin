
#ifndef SPINWEB_PASSWORD_H
#define SPINWEB_PASSWORD_H 1

/*
 * Helper functions for file reading
 */

/*
 * Data structure to pass from and to the file scanner
 * functions, which check whether a file exists (for instance,
 * to find index.html for a query that does not have it specified)
 * Any informative data that is found (such as whether it's a gzipped
 * file) can be placed into this structure)
 */
typedef struct {
    FILE* fp;
    int gzipped;
} web_file_t;

/*
 * Returns opened FILE* pointer if the path exists and is a regular
 * file.
 * NULL otherwise, or if fopen() fails.
 */
FILE* try_file(const char* path);

/*
 * Tries whether the file path exists, and if not, whether the path
 * with '.html', or (if url ends with /) 'index.html' exists, in that order
 * Returns open file pointer if so, NULL if not
 */
FILE* try_files(const char* base_path, const char* path);
void try_files2(const char* base_path, const char* path, web_file_t* web_file);


/*
 * Check a username and password against the given file
 * File should be in unix password file format
 * Returns 1 if password matches
 */
int check_password(const char* password_file_name, const char* username, const char* password);

/*
 * Returns the size of a file
 */
long get_file_size(const char *filename);

/*
 * Returns the entire contents of a file as a string
 * Caller must free the data
 */
char *read_file(const char *filename);


#endif // SPINWEB_PASSWORD_H