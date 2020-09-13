#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

size_t safe_write(int fd, const unsigned char *buf, size_t amt) {
   ssize_t ret;
   size_t written = 0;
   while (written != amt) {
      ret = write(fd, buf + written, amt - written);
      if (ret < 0) {
         break;
      }
      written += ret;
   }
   return written;
}

/* THIS NEEDS TO BE REWRITTED TO WORK WITH NON_BLOCKING IO */
ssize_t safe_read(int fd, unsigned char *buf, size_t amt) {
   ssize_t ret;
   size_t readn = 0;
   while (readn != amt) {
      ret = read(fd, buf + readn, amt - readn);
      if (ret < 0) {
         if (readn == 0) {
	    return -1;
	 }
	 break;
      }
      if (ret == 0) {
         break;
      }
      readn += ret;
   }
   return readn;
}

size_t split_string(char *delim, char *str, char **strings, size_t n) {
   char *cur;
   size_t i = 0;
   do {
      cur = strtok(str, delim);
      *(strings++) = str;
      ++i;
   } while (cur != NULL && i < n);
   return i;
}

