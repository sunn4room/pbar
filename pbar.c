#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

struct bar {
    const char* const version;
};

enum {
    NO_ERROR,
    INNER_ERROR,
    RUNTIME_ERROR,
};

static void quit(struct bar* bar, const int code, const char* restrict fmt, ...)
{
    if (fmt != NULL) {
        va_list ap;
        va_start(ap, fmt);
        vfprintf(stderr, fmt, ap);
        va_end(ap);
    }

    exit(code);
}

static void init(struct bar* bar)
{
    struct stat stdin_stat;
    fstat(STDIN_FILENO, &stdin_stat);
    if (!S_ISFIFO(stdin_stat.st_mode)) {
        quit(bar, NO_ERROR,
            "pbar is a wayland statusbar that renders plain text from stdin and prints mouse event action to stdout.\n"
            "pbar version: %s\n",
            bar->version);
    }
}

int main()
{
    struct bar bar = {
        .version = "0.1",
    };

    init(&bar);

    return NO_ERROR;
}
