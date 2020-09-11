#ifndef UTIL_H
#define UTIL_H
#include <stdlib.h>

size_t safe_write(int fd, const unsigned char *buf, size_t amt);
size_t safe_read(int fd, unsigned char *buf, size_t amt);

#endif
