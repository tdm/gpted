#ifndef UTIL_H
#define UTIL_H

#include "types.h"

#define ROUNDUP(x,n) ((((x) + (n)-1) / (n)) * (n))

void hexdump(const byte *buf, uint32_t len);

unsigned long int strtoul_u(const char *p, char **endp, int base);
unsigned long long int strtoull_u(const char *p, char **endp, int base);

#endif
