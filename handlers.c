#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <string.h>
#include "enviroment.h"
#include "telnet_client.h"
#include "terminal.h"
#include "handlers.h"

static int put_in_raw_mode(terminal_state *state) {
   if (state == NULL) {
      return 0;
   }
   if (tty_raw_mode(state) < 0) {
      return -1;
   } 
   return 0;
}

static int put_in_default_mode(terminal_state *state) {
   if (state == NULL) {
      return 0;
   }
   if (tty_default_mode(state) < 0) {
      return -1;
   } 
   return 0;
}

static int echo_opt_handler(unsigned char cmd, void *args, int *do_sb) {
   int ret;
   *do_sb = 0; 
   switch (cmd) {
      case WILL:
	 if (put_in_raw_mode((terminal_state *)args) < 0) {
	    ret = DONT;
	 } else {
	    ret = DO;
	 }
	 break;
      case WONT:
	 ret = DONT;
	(void)put_in_default_mode((terminal_state *)args);
	 break;
      case DO:
	 ret = WONT;
	 break;
      case DONT:
	 ret = WONT;
	 break;
   }
   return ret;
}

struct opt_handler *new_echo_opt_handler(terminal_state *state) {
   struct opt_handler *handler = opt_handler_new();
   if (handler == NULL) {
      return NULL;
   }
   handler->opt_handler = echo_opt_handler;
   handler->opt_args = (void *)state;
   handler->optcode = TELOPT_ECHO;
   return handler;
}

struct naws_callback {
   void (*sig_handler)(int);
};

static int setup_handler(struct naws_callback *callback) {
   struct sigaction sig;
   memset(&sig, 0, sizeof(sig));
   sig.sa_handler = callback->sig_handler;
   sig.sa_flags |= SA_RESTART;
   if (sigaction(SIGWINCH, &sig, NULL) < 0) {
      return -1;
   }
   return 0;
}

int naws_opt_handler(unsigned char cmd, void *args, int *do_sb) {
   int ret;
   struct naws_callback *callback = args;
   *do_sb = 1;
   switch (cmd) {
      case DO:
	 if (setup_handler(callback) < 0) {
	    ret = WONT;
	 } else {
	    ret = WILL;
	 }
	 break;
      case WILL:
	 ret = DONT;
	 break;
      case DONT:
	 *do_sb = 0;
	 ret = WONT;
	 break;
      case WONT:
	 *do_sb = 0;
	 ret = DONT;
	 break;
   }
   return ret;
}

static int get_window_size(int fd, uint16_t *row, uint16_t *col) {
   struct winsize size;
   if (ioctl(fd, TIOCGWINSZ, (char *)&size) < 0) {
      return -1;
   }
   *row = size.ws_row;
   *col = size.ws_col;
   return 0;
}

static unsigned char naws_buf[4];
static sbresult_l naws_buf_size = 4;

static unsigned char *naws_sb_handler_start(void *args, sbresult_l *resplen) {
   uint16_t row, col;
   if (get_window_size(STDIN_FILENO, &row, &col) < 0) {
      *resplen = -1;
      return NULL;
   }
   naws_buf[0] = col >> 8;
   naws_buf[1] = 0xff & col;
   naws_buf[2] = row >> 8;
   naws_buf[3] = 0xff & row;
   *resplen = naws_buf_size;
   return naws_buf;
}

static void naws_args_free(void *opt_args, void *sb_args) {
   free(opt_args);
}

struct opt_handler *new_naws_opt_handler(void(*sig_handler)(int)) {
   struct opt_handler *handler;
   struct naws_callback *callback = malloc(sizeof(struct naws_callback));
   if (callback == NULL) {
      return NULL;
   }
   handler = opt_handler_new();
   if (handler == NULL) {
      free(callback);
      return NULL;
   }
   callback->sig_handler = sig_handler;
   handler->opt_handler = naws_opt_handler;
   handler->sb_handler_start = naws_sb_handler_start;
   handler->opt_args = (void *)callback;
   handler->args_free = naws_args_free;
   handler->optcode = TELOPT_NAWS;
}

static int termtype_opt_handler(unsigned char cmd, void *args, int *do_sb) {
   int ret;
   *do_sb = 0;
   switch (cmd) {
      case DO:
	   if (get_env("TERM") == NULL) {
	      ret = WONT;
	   } else {
	      ret = WILL;
	   }
	   break;
       case DONT:
	   ret = WONT;
	   break;
       case WILL:
       case WONT:
	   ret = DONT;
	   break;
   }
   return ret;
}

#define TERMTYPEBASE 1

static unsigned char *termtype_sb_handler_resp(unsigned char *sent, void *args, size_t len, sbresult_l *resplen) {
   unsigned char *resp_buf;
   size_t terminal_type_len;
   const char *terminal_type = get_env("TERM");
   if (terminal_type == NULL) {
      *resplen = -1;
      return NULL;
   }
   terminal_type_len = strlen(terminal_type);
   resp_buf = malloc(sizeof(unsigned char) * (TERMTYPEBASE + terminal_type_len));
   if (resp_buf == NULL) {
      *resplen = -1;
      return NULL;
   }
   resp_buf[0] = IS;
   memcpy(resp_buf + 1, terminal_type, terminal_type_len);
   *resplen = TERMTYPEBASE + terminal_type_len;
   return resp_buf;
}

struct opt_handler *new_termtype_opt_handler(void) {
   struct opt_handler *handler = opt_handler_new();
   if (handler == NULL) {
      return NULL;
   }
   handler->opt_handler = termtype_opt_handler;
   handler->sb_handler_resp = termtype_sb_handler_resp;
   handler->sb_free_buf = free;
   handler->optcode = TELOPT_TERMTYPE;
   return handler;
}





























