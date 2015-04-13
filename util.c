#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <limits.h>
#include <errno.h>

#include "util.h"

void hexdump(const byte *buf, uint32_t len)
{
    uint32_t x, y;
    for (y = 0; y*16 < len; ++y) {
        for (x = 0; x < 16 && y*16+x < len; ++x) {
            printf("%02x ", buf[y*16+x]);
        }
        printf("\n");
    }
}

unsigned int user_multiplier(char **p)
{
    char c = **p;
    switch (c)
    {
    case 's': case 'S':
        ++(*p);
        return 512;
    case 'k': case 'K':
        ++(*p);
        return 1024;
    case 'm': case 'M':
        ++(*p);
        return 1024*1024;
    case 'g': case 'G':
        ++(*p);
        return 1024*1024*1024;
    default:
        break;
    }
    return 1;
}

unsigned long int strtoul_u(const char *p, char **endp, int base)
{
    char *u_endp;
    unsigned long int val;
    unsigned int mul;
    val = strtoul(p, &u_endp, base);
    if (val == ULONG_MAX || errno == ERANGE) {
        return val;
    }
    mul = user_multiplier(&u_endp);
    if (endp)
        *endp = u_endp;
    return val * mul;
}

unsigned long long int strtoull_u(const char *p, char **endp, int base)
{
    char *u_endp;
    unsigned long long int val;
    unsigned int mul;
    val = strtoull(p, &u_endp, base);
    if (val == ULLONG_MAX || errno == ERANGE) {
        return val;
    }
    mul = user_multiplier(&u_endp);
    if (endp)
        *endp = u_endp;
    return val * mul;
}
