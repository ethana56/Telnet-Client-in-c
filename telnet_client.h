#ifndef TELNET_CLIENT_H
#define TELNET_CLIENT_H

#include <stdlib.h>

#define SE       240
#define NOP      241
#define DATAMARK 242
#define BRK      243
#define IP       244
#define AO       245
#define AYT      246
#define EC       247
#define EL       248
#define GA       249
#define SB       250
#define WILL     251
#define WONT     252
#define DO       253
#define DONT     254

typedef long sbresult_l;
#define SBRESULT_MAX LONG_MAX

struct opt_handler {
   int(*opt_handler)(unsigned char, void *, int *);
   unsigned char *(*sb_handler_resp)(unsigned char *, void *, size_t len, sbresult_l *resplen);
   unsigned char *(*sb_handler_start)(void *, sbresult_l *resplen);
   void *opt_args;
   void *sb_args;
   void (*args_free)(void *, void *);
   void (*sb_free_buf)(void *);
   int optcode;
};

typedef struct telnet_client TelnetClient;

TelnetClient *new_TelnetClient(void);
struct opt_handler *opt_handler_new(void);
int telnet_reg_opt_handler(TelnetClient *, struct opt_handler *);
int telnet_create_connection(TelnetClient *, char *, char *);
int telnet_get_sockfd(TelnetClient *);
int telnet_start_sb(TelnetClient *, unsigned char);
int telnet_send(TelnetClient *,  unsigned char *, size_t);
int telnet_receive(TelnetClient *, unsigned char *, size_t, size_t *);
void telnet_free(TelnetClient *);

#endif
