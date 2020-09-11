#include <stdlib.h>
#include <stdio.h>
#include "telnet_client.h"

static int default_handler_func(unsigned char cmd, void *args, int *do_sb) {
   int ret;
   switch (cmd) {
      case DO:
	      ret = WONT;
	      break;
      case WILL:
	      ret = DONT;
	      break;
      case DONT:
	      ret = WONT;
	      break;
      case WONT:
	      ret = DONT;
	      break;
   }
   *do_sb = 0;
   return ret;
}

void init_default_opt_handler(struct opt_handler *handler) {
   handler->opt_handler = default_handler_func;
   handler->sb_handler_resp = NULL;
   handler->sb_handler_start = NULL;
   handler->opt_args = NULL;
   handler->sb_args = NULL;
   handler->sb_free_buf = NULL;
   handler->optcode = -1;
}
