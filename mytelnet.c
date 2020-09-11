#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <termios.h>
#include <errno.h>
#include "telnet.h"

struct args {
   char *addr;
   char *port;
};

void exit_err(char *str) {
   perror(str);
   exit(EXIT_FAILURE);
}

static int parse_args(int argc, char *argv[], struct args *args) {
   if (argc == 1) {
      return -1;
   }
   args->addr = argv[1];
   if (argc < 3) {
      args->port = NULL;
   } else {
      args->port = argv[2];
   }
   return 0;
}

int main(int argc, char *argv[]) {
   struct args args;
   if (parse_args(argc, argv, &args) < 0) {
      fprintf(stderr, "USAGE\n");
      exit(EXIT_FAILURE);
   }
   start_telnet(args.addr, args.port);
   return 0;
}











