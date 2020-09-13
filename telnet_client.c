#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <ctype.h> /* temp: only needed for testing */
#include <signal.h>
#include <fcntl.h>
#include "util.h"
#include "terminal.h"
#include "telnet_client.h"
#include "default_opt_handler.h"

#define NUL 0
#define LF  10
#define CR  13
#define BEL 7
#define BS  8
#define HT  9
#define VT  11
#define FF  12

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

#define SBBUFINCREASE 30

#define IAC 255

#define ISCONTROL(s) (((s) == NOP) || ((s) == BRK) || ((s) == GA))
#define ISNEGOTIATION(s) (((s) == WILL) || ((s) == WONT) || ((s) == DO) || ((s) == DONT) || \
                 ((s) == SB))
#define ISSUPNEGOTIATION(s) (((s) == WILL) || ((s) == WONT) || ((s) == DO) || ((s) == DONT))

static struct opt_handler default_opt_handler;
int opt_handler_set = 0;

enum cmd_state {cmd_s, opt_s, sb_s, ready_s};

struct cmd_data {
   unsigned char *sb_buf;
   size_t sb_buf_size;
   size_t sb_buf_len;
   unsigned char cmd;
   unsigned char opt;
   int sb_state;
   enum cmd_state state;
};

struct telnet_client {
   struct cmd_data cmd_data;
   struct opt_handler **handlers;
   size_t handlers_size;
   int sockfd;
   int state;
   int adj_iac_state;
};

void init_cmd_data(struct cmd_data *cmd_data) {
   cmd_data->sb_buf = NULL;
   cmd_data->sb_buf_size = 0;
   cmd_data->sb_buf_len = 0;
   cmd_data->sb_state = 0;
   cmd_data->state = cmd_s;
}

TelnetClient *new_TelnetClient(void) {
   TelnetClient *client = malloc(sizeof(TelnetClient));
   if (client == NULL) {
      return NULL;
   }
   init_cmd_data(&client->cmd_data);
   client->handlers = NULL;
   client->sockfd = -1;
   client->handlers_size = 0;
   client->state = 0;
   client->adj_iac_state = 0;
   if (!opt_handler_set) {
      init_default_opt_handler(&default_opt_handler);
      opt_handler_set = 1;
   }
   return client;
}

struct opt_handler *opt_handler_new(void) {
   struct opt_handler *handler = malloc(sizeof(struct opt_handler));
   if (handler == NULL) {
      return NULL;
   }
   handler->opt_handler = NULL;
   handler->sb_handler_resp = NULL;
   handler->sb_handler_start = NULL;
   handler->opt_args = NULL;
   handler->sb_args = NULL;
   handler->args_free = NULL;
   handler->sb_free_buf = NULL;
   handler->optcode = -1;
   return handler;
}

static void free_opt_handler(struct opt_handler *handler) {
   if (handler->args_free != NULL) {
      handler->args_free(handler->opt_args, handler->sb_args);
   }
   free(handler);
}

static void cleanup_handlers(TelnetClient *client) {
   size_t i;
   for (i = 0; i < client->handlers_size; ++i) {
      if (client->handlers[i] != &default_opt_handler) {
	 free_opt_handler(client->handlers[i]);
      }
   }
   free(client->handlers);
}

static void cleanup_cmd_data(struct cmd_data *cmd_data) {
   free(cmd_data->sb_buf);
}

void telnet_free(TelnetClient *client) {
   cleanup_handlers(client);
   cleanup_cmd_data(&client->cmd_data);
   close(client->sockfd);
   free(client);
}

static void set_handler_array(struct opt_handler **handlers, int size) {
   int i;
   for (i = 0; i < size; ++i) {
      handlers[i] = &default_opt_handler;
   }
}

static struct opt_handler *get_opt_handler(TelnetClient *client, int opt) {
   if (opt > client->handlers_size - 1) {
      return &default_opt_handler;
   }
   if (client->handlers == NULL) {
      return &default_opt_handler;
   } 
   return client->handlers[opt];
}

int telnet_reg_opt_handler(TelnetClient *client, struct opt_handler *handler) {
   if (client->handlers == NULL) {
      client->handlers = malloc(sizeof(struct opt_handler *) * (handler->optcode + 1));
      if (client->handlers == NULL) {
         return -1;
      }
      client->handlers_size = handler->optcode + 1;
      set_handler_array(client->handlers, client->handlers_size);
   } else if (client->handlers_size < handler->optcode + 1) {
      client->handlers = realloc(client->handlers, sizeof(struct opt_handler *) * (handler->optcode + 1));
      if (client->handlers == NULL) {
         return -1;
      }
      set_handler_array(client->handlers + client->handlers_size, (handler->optcode + 1) - (client->handlers_size));
      client->handlers_size = handler->optcode + 1;
   }
   client->handlers[handler->optcode] = handler;
   return 0;
}

static int set_nonblock(int fd) {
   int status;
   status = fcntl(fd, F_GETFL, 0);
   if (status == -1) {
      return -1;
   }
   status = fcntl(fd, F_SETFL, status | O_NONBLOCK);
   if (status == -1) {
      return -1;
   }
   return 0;
}

int telnet_create_connection(TelnetClient *client, char *addr, char *port) {
   struct addrinfo hints, *res0, *res;
   int sockfd;
   char *host = addr;
   char *service = port ? port : "telnet";
   
   memset(&hints, 0, sizeof(hints));
   hints.ai_family = AF_INET;
   hints.ai_socktype = SOCK_STREAM;
   hints.ai_flags |= AI_CANONNAME;
   hints.ai_protocol = IPPROTO_TCP;
   if (getaddrinfo(host, service, &hints, &res0) < 0) {
      return -1;
   }
   for (res = res0; res; res = res->ai_next) {
      sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
      if (sockfd < 0) {
         continue;
      }
      if (connect(sockfd, res->ai_addr, res->ai_addrlen) < 0) {
         close(sockfd);
         sockfd = -1;
         continue;
      }
      break;
   }
   freeaddrinfo(res0);
   if (sockfd < 0) {
      return -1;
   }
   if (set_nonblock(sockfd) < 0) {
      close(sockfd);
      return -1;
   }
   client->sockfd = sockfd;
   return 0;
}

int telnet_get_sockfd(TelnetClient *client) {
   return client->sockfd;
}

struct send_buf {
   unsigned char *buf;
   size_t len;
   size_t size;
};

static int buffered_send(int fd, struct send_buf *buf_data, unsigned char byte) {
   buf_data->buf[buf_data->len++] = byte;
   if (buf_data->len == buf_data->size) {
      if (safe_write(fd, buf_data->buf, buf_data->len) != buf_data->len) {
         return -1;
      }
      buf_data->len = 0;
   }
   return 0;
}

static int flush_send_buffer(int fd, struct send_buf *buf_data) {
   if (buf_data->len == 0) {
      return 0;
   }
   if (safe_write(fd, buf_data->buf, buf_data->len) != buf_data->len) {
      return -1;
   }
   buf_data->len = 0;
   return 0;
}

int telnet_send(TelnetClient *client, unsigned char *buf, size_t n) {
   unsigned char intern_buf[10];
   size_t i;
   struct send_buf buf_data;

   buf_data.len = 0;
   buf_data.buf = intern_buf;
   buf_data.size = sizeof(intern_buf);

   for (i = 0; i < n; ++i) {
      if (buffered_send(client->sockfd, &buf_data, buf[i]) < 0) {
         return -1;
      }
      if (buf[i] == IAC) {
         if (buffered_send(client->sockfd, &buf_data, IAC) < 0) {
	    return -1;
	 }
      }
   }
   flush_send_buffer(client->sockfd, &buf_data);
   return 0;
}

static int add_to_sb_buf(struct cmd_data *cmd_data, unsigned char byte) {
   if (cmd_data->sb_buf_size == 0) {
      cmd_data->sb_buf = malloc(sizeof(unsigned char) * SBBUFINCREASE);
      if (cmd_data->sb_buf == NULL) {
         return -1;
      }
      cmd_data->sb_buf_size = SBBUFINCREASE;
   } else if (cmd_data->sb_buf_size == cmd_data->sb_buf_len) {
      cmd_data->sb_buf = realloc(cmd_data->sb_buf, cmd_data->sb_buf_size + SBBUFINCREASE);
      if (cmd_data->sb_buf == NULL) {
         return -1;
      }
      cmd_data->sb_buf_size += SBBUFINCREASE;
   }
   cmd_data->sb_buf[cmd_data->sb_buf_len++] = byte;
   return 0;
}

static void handle_cmd_s(struct cmd_data *cmd_data, unsigned char byte) {
   cmd_data->cmd = byte;
   if (ISCONTROL(byte)) {
      cmd_data->state = ready_s;
   } else {
      cmd_data->state = opt_s;
   }
}

static void handle_opt_s(struct cmd_data *cmd_data, unsigned char byte) {
   cmd_data->opt = byte;
   if (cmd_data->cmd == SB) {
      cmd_data->state = sb_s;
   } else {
      cmd_data->state = ready_s;
   }
}

static int handle_sb_s(struct cmd_data *cmd_data, unsigned char byte) {
   int ret = 0;
   if (cmd_data->sb_state == IAC && byte == IAC) {
      ret = add_to_sb_buf(cmd_data, byte);
      cmd_data->sb_state = 0;
   } else if (cmd_data->sb_state == IAC && byte == SE) {
      cmd_data->state = ready_s;
      cmd_data->sb_state = 0;
   } else if (byte == IAC) {
      cmd_data->sb_state = IAC;
   } else {
      ret = add_to_sb_buf(cmd_data, byte);
   }
   return ret;
}

static void reset_cmd_data(struct cmd_data *cmd_data) {
   cmd_data->sb_buf_len = 0;
   cmd_data->state = cmd_s;
}

static int handle_cmd(TelnetClient *client, unsigned char byte) {
   int ret = 0;
   switch (client->cmd_data.state) {
	   case cmd_s:
		   handle_cmd_s(&client->cmd_data, byte);
		   break;
           case opt_s:
                   handle_opt_s(&client->cmd_data, byte);
		   break;
	   case sb_s:
		   ret = handle_sb_s(&client->cmd_data, byte);
		   break;
	   default:
		   ret = -1;
   }
   if (ret < 0) {
      ret = -1;
   } else if (client->cmd_data.state == ready_s) {
      ret = 1;   
   }
   return ret;
}

static int send_beg_meta(int fd, struct opt_handler *handler, struct send_buf *buf_data) {
   if (buffered_send(fd, buf_data, IAC) < 0||
       buffered_send(fd, buf_data, SB) < 0||
       buffered_send(fd, buf_data, handler->optcode) < 0) {
      return -1;
   }
   return 0;
}

static int send_end_meta(int fd, struct send_buf *buf_data) {
   if (buffered_send(fd, buf_data, IAC) < 0 ||
       buffered_send(fd, buf_data, SE) < 0) {
      return -1;
   }
   return 0;
}

static int send_sb_with_meta(int fd, struct opt_handler *handler, unsigned char *buf, size_t len) {
   size_t i;
   unsigned char intern_buf[15];
   struct send_buf buf_data;
   buf_data.buf = intern_buf;
   buf_data.size = sizeof(intern_buf);
   buf_data.len = 0;

   if (send_beg_meta(fd, handler, &buf_data) < 0) {
      return -1;
   }
   for (i = 0; i < len; ++i) {
      if (buffered_send(fd, &buf_data, buf[i]) < 0) {
         return -1;
      }
      if (buf[i] == IAC) {
         if (buffered_send(fd, &buf_data, IAC) < 0) {
	    return -1;
	 }
      }
   }
   if (send_end_meta(fd, &buf_data) < 0) {
      return -1;
   }
   if (flush_send_buffer(fd, &buf_data) < 0) {
      return -1;
   }
   return 0;
}

static int execute_sb_start(TelnetClient *client, struct opt_handler *opt_handler) {
   unsigned char *result;
   sbresult_l length;
   int ret = 0;
   result = opt_handler->sb_handler_start(opt_handler->sb_args, &length);
   if (length < 0) {
      return -1;
   }
   if (length == 0) {
      return 0;
   }
   if (send_sb_with_meta(client->sockfd, opt_handler, result, length) < 0) {
      ret = -1;
   }  
   if (opt_handler->sb_free_buf != NULL) {
      opt_handler->sb_free_buf(result);
   }
   return ret; 
}

static int execute_sb_resp(TelnetClient *client, struct opt_handler *opt_handler) {
   unsigned char *result;
   sbresult_l length;
   int ret = 0;
   struct cmd_data *cmd_data = &client->cmd_data;
   result = opt_handler->sb_handler_resp(cmd_data->sb_buf, opt_handler->sb_args, cmd_data->sb_buf_len, &length);
   if (length < 0) {
      return -1;
   }
   if (length == 0) {
      return 0;
   }
   if (send_sb_with_meta(client->sockfd, opt_handler, result, length) < 0) {
      ret = -1;
   } 
   if (opt_handler->sb_free_buf != NULL) {
      opt_handler->sb_free_buf(result);
   } 
   return ret;
}

static int execute_neg(TelnetClient *client, struct opt_handler *opt_handler) {
   int result, do_sb;
   unsigned char neg_buf[3];
   size_t write_ret;
   result = opt_handler->opt_handler(client->cmd_data.cmd, opt_handler->opt_args, &do_sb);
   if (result < 0) {
      return -1;
   }
   neg_buf[0] = IAC;
   neg_buf[1] = result;
   neg_buf[2] = client->cmd_data.opt;
   write_ret = safe_write(client->sockfd, neg_buf, sizeof(neg_buf));
   if (write_ret != sizeof(neg_buf)) {
      return -1;
   }
   if (!do_sb) {
      return 0;
   } 
   return execute_sb_start(client, opt_handler);
}

int telnet_start_sb(TelnetClient *client, unsigned char opt) {
   struct opt_handler *handler = get_opt_handler(client, opt);
   if (handler == NULL) {
      return -1;
   }
   if (execute_sb_start(client, handler) < 0) {
      return -1;
   }
   return 0;
}

static int execute_negotiation(TelnetClient *client) {
   int ret = -1;
   struct cmd_data *cmd_data = &client->cmd_data;
   struct opt_handler *opt_handler = get_opt_handler(client, cmd_data->opt);
   if (cmd_data->cmd == SB) {
      ret = execute_sb_resp(client, opt_handler);
   } else if (ISSUPNEGOTIATION(cmd_data->cmd)) {
      ret = execute_neg(client, opt_handler);
   }
   return ret;
}

static int execute_cmd(TelnetClient *client) {
   int ret = 0;
   if (ISNEGOTIATION(client->cmd_data.cmd)) {
      ret = execute_negotiation(client);
   }
   return ret;
}

static int do_iac_state(TelnetClient *client, unsigned char cmd) {
   int result, ret = 0;
   result = handle_cmd(client, cmd);
   if (result < 0) {
      ret = -1;
   } else if (result) {
      ret = execute_cmd(client);
      client->state = 0;
      reset_cmd_data(&client->cmd_data);
   }
   return ret;
}

static int process_iac_state(TelnetClient *client, unsigned char cmd) {
   int result;
   client->adj_iac_state = 0;
   result = do_iac_state(client, cmd);
   if (result < 0) {
      return -1;
   }
   if (result) {
      client->state = 0;
   }
   return 0;
}

static int cpy_bytes(TelnetClient *client, unsigned char *intern_buf, 
		size_t amt, unsigned char *extern_buf, size_t *cpylen) {
   size_t i, amt_cpy;
   int ret = 0;
   for (amt_cpy = 0, i = 0; i < amt; ++i) {
      if (client->adj_iac_state && intern_buf[i] == IAC) {
         extern_buf[amt_cpy++] = intern_buf[i];
         client->state = 0;
	 client->adj_iac_state = 0;
      } else if (client->state == IAC) {
	 if (process_iac_state(client, intern_buf[i]) < 0) {
	    ret = -1;
	    break;
	 } 
      } else if (intern_buf[i] == IAC) {
         client->state = IAC;
	 client->adj_iac_state = 1;
      } else {
         extern_buf[amt_cpy++] = intern_buf[i];
      }
   }
   *cpylen = amt_cpy;
   return ret;
}

int telnet_receive(TelnetClient *client, unsigned char *extern_buf, size_t extern_bufsiz, size_t *amt) {
   int ret = 1;
   unsigned char intern_buf[BUFSIZ];
   ssize_t amt_read = 0;
   size_t total_read = 0, total_cpy = 0;
   size_t amt_to_read, amt_cpy;

   while (total_read < extern_bufsiz) {
      amt_to_read = (extern_bufsiz - total_read) > sizeof(intern_buf) ? sizeof(intern_buf) : extern_bufsiz - total_read;
      amt_read = safe_read(client->sockfd, intern_buf, amt_to_read);
      if (amt_read < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
         break;
      }
      if (amt_read < 0) {
         ret = -1;
	 break;
      }
      if (amt_read == 0) { 
         ret = 0;
	 break;
      }
      total_read += amt_read;
      if (cpy_bytes(client, intern_buf, amt_read, extern_buf + total_cpy, &amt_cpy) < 0) {
         ret = -1;
	 total_cpy += amt_cpy;
	 break;
      }
      total_cpy += amt_cpy;
   }
   *amt = total_cpy;
   return ret;
}






