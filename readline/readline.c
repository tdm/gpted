#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "readline.h"

char *readline(const char *prompt)
{
    char *buf = NULL;
    size_t n = 0;
    ssize_t len;

    if (prompt && *prompt) {
        fprintf(stdout, "%s", prompt);
        fflush(stdout);
    }
    len = getline(&buf, &n, stdin);
    if (len == -1) {
        free(buf);
        buf = NULL;
    }
    else {
        char *p = strchr(buf, '\n');
        if (p)
            *p = '\0';
    }

    return buf;
}
