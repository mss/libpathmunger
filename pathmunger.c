#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <dlfcn.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdarg.h>
#include <malloc.h>
/*#include <openat2.h>*/

// List of path replacement pairs.  The first item is the prefix, the
// second the replacement string.  When the prefix ends with a slash,
// it is compared against the beginning of the path; else the whole
// path is compared.  If these are equal, the prefix is replaced with
// the replacement.
static const char *path_map[][2] = {
    { "/etc/os-release", "/tmp/os-release" },
    { "/etc/os-release.d/", "/tmp/os-release.d/" },
};

static int debug = 0;

static __thread char *buffer = NULL;
static __thread size_t buffer_size = 0;


void __attribute__((constructor)) init() {
    debug = getenv("LIBPATHMUNGER_DEBUG") != NULL;
}


static size_t match(const char *path, const char *prefix) {
    const size_t prefix_len = strlen(prefix);
    if (prefix[prefix_len - 1] != '/') {
        return strcmp(path, prefix) == 0 ? prefix_len : 0;
    }
    else {
        const size_t path_len = strlen(path);
        if (path_len == prefix_len - 1) {
            return strncmp(path, prefix, path_len) == 0 ? path_len : 0;
        }
        else if (path_len < prefix_len) {
            return 0;
        }
        else {
            return strncmp(path, prefix, prefix_len) == 0 ? prefix_len : 0;
        }
    }
}

static char *get_buffer(size_t min_size) {
    const size_t slack = 63;
    if (min_size == 0) {
        min_size = 1;
    }
    if (min_size > buffer_size) {
        size_t new_size = min_size + slack;
        if (buffer != NULL) {
            free(buffer);
            buffer = NULL;
            buffer_size = 0;
        }
        buffer = malloc(new_size);
        if (buffer != NULL) {
            buffer_size = new_size;
        }
    }
    return buffer;
}

static const char *fix_path(const char *path) {
    if (path == NULL)
        return NULL;
    int count = (sizeof path_map) / (sizeof *path_map); // Array length
    for (int i = 0; i < count; i++) {
        const char *prefix = path_map[i][0];
        const char *replace = path_map[i][1];
        const size_t match_len = match(path, prefix);
        if (match_len) {
            char *new_path = get_buffer(strlen(path) + strlen(replace) - strlen(prefix));
            if (new_path == NULL) {
                return path;
            }
            strncpy(new_path, replace, match_len);
            strcat(new_path, path + match_len);
            if (debug) {
                fprintf(stderr, "Mapped Path: %s  ==>  %s\n", path, new_path);
            }
            return new_path;
        }
    }
    return path;
}


// varargs => special case!
typedef int (*orig_open_func)(const char *pathname, int flags, ...);
int open(const char *pathname, int flags, ...) {
    const char *new_path = fix_path(pathname);

    static orig_open_func orig_func = NULL;
    if (orig_func == NULL)
        orig_func = (orig_open_func)dlsym(RTLD_NEXT, "open");

    // If O_CREAT is used to create a file, the file access mode must be given.
    if (flags & O_CREAT) {
        va_list args;
        va_start(args, flags);
        mode_t mode = va_arg(args, mode_t);
        va_end(args);
        return orig_func(new_path, flags, mode);
    } else {
        return orig_func(new_path, flags);
    }
}
int open64(const char *pathname, int flags, ...) {
    const char *new_path = fix_path(pathname);

    static orig_open_func orig_func = NULL;
    if (orig_func == NULL)
        orig_func = (orig_open_func)dlsym(RTLD_NEXT, "open64");

    // If O_CREAT is used to create a file, the file access mode must be given.
    if (flags & O_CREAT) {
        va_list args;
        va_start(args, flags);
        mode_t mode = va_arg(args, mode_t);
        va_end(args);
        return orig_func(new_path, flags, mode);
    } else {
        return orig_func(new_path, flags);
    }
}
typedef int (*orig_openat_func)(int dirfd, const char *pathname, int flags, ...);
int openat(int dirfd, const char *pathname, int flags, ...) {
    const char *new_path = fix_path(pathname);

    static orig_openat_func orig_func = NULL;
    if (orig_func == NULL)
        orig_func = (orig_openat_func)dlsym(RTLD_NEXT, "openat");

    // If O_CREAT is used to create a file, the file access mode must be given.
    if (flags & O_CREAT) {
        va_list args;
        va_start(args, flags);
        mode_t mode = va_arg(args, mode_t);
        va_end(args);
        return orig_func(dirfd, new_path, flags, mode);
    } else {
        return orig_func(dirfd, new_path, flags);
    }
}

// general hook stuff

// can't write C without committing some crimes against code standards
#define DEFINE_HOOK(rett, name, arglist, ...) \
    rett name(__VA_ARGS__) { \
        pathname = fix_path(pathname); \
        static void *orig_func = NULL; \
        if (orig_func == NULL) orig_func = dlsym(RTLD_NEXT, #name); \
        return ((rett (*)(__VA_ARGS__))(orig_func)) arglist; \
    } \

/*DEFINE_HOOK(int, openat2, (dirfd, pathname, how, size),
        int dirfd, const char* pathname, const struct open_how* how, size_t size)*/

DEFINE_HOOK(int, creat, (pathname, mode), const char* pathname, mode_t mode)

DEFINE_HOOK(int, stat, (pathname, statbuf), const char* pathname, struct stat* statbuf)

DEFINE_HOOK(int, lstat, (pathname, statbuf), const char* pathname, struct stat* statbuf)

// ...

DEFINE_HOOK(FILE*, fopen, (pathname, mode), const char* pathname, const char* mode)

DEFINE_HOOK(FILE*, freopen, (pathname, mode, stream),
        const char* pathname, const char* mode, FILE* stream)

