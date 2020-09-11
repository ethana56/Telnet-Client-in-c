#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include "telnet.h"
#include "terminal.h"
#include "telnet_client.h"
#include "util.h"
#include "handlers.h"
#include "enviroment.h"

static volatile sig_atomic_t sigwinch = 0;

static terminal_state term_state;
static int terminal_state_set = 0;

static void sigwinch_handler(int signo) {
   sigwinch = 1;
}

static int setup_terminal(int, int);
static int handle_client(TelnetClient *);
static int handle_in(TelnetClient *);

static int setup_signals(sigset_t *sigset) {
   sigset_t set; 
   if (sigemptyset(&set) < 0) {
      return -1;
   }
   if (sigaddset(&set, SIGWINCH) < 0) {
      return -1;
   }
   if (sigprocmask(SIG_BLOCK, &set, sigset) < 0) {
      return -1;
   }
   return 0;
}

static int handle_sigs(TelnetClient *client) {
   if (sigwinch == 1) {
      if (telnet_start_sb(client, TELOPT_NAWS) < 0) {
         return -1;
      }
      sigwinch = 0;
   }
   return 0;
}

static int start(TelnetClient *client) {
   fd_set savedset, set;
   int err, result;
   int sockfd;
   sigset_t sigset;
   sockfd = telnet_get_sockfd(client);

   FD_ZERO(&savedset);
   FD_SET(sockfd, &savedset);
   FD_SET(STDIN_FILENO, &savedset);
   
   if (setup_signals(&sigset) < 0) {
      return -1;
   }

   while (1) {
      set = savedset;
      err = pselect(sockfd + 1, &set, NULL, NULL, NULL, &sigset);
      if (err < 0 && errno == EINTR) {
	 if (handle_sigs(client) < 0) {
	    return -1;
	 }
         continue;
      }
      if (err < 0) {
         return -1;
      }
      if (FD_ISSET(sockfd, &set)) {
	 result = handle_client(client);
         if (result < 0) {
	    return -1;
	 }
	 if (!result) {
	    break;
	 }
      }
      if (FD_ISSET(STDIN_FILENO, &set)) {
	 result = handle_in(client);
         if (result < 0) {
	    return -1;
	 }
	 if (!result) {
	    break;
	 }
      }
   }
   return 0;
}

static int register_negotiations(TelnetClient *);

void start_telnet(char *addr, char *port) {
   TelnetClient *client = new_TelnetClient();
   if (client == NULL) {
      perror(NULL);
      exit(EXIT_FAILURE);
   }
   if (setup_terminal(STDIN_FILENO, STDOUT_FILENO) < 0) {
      goto err;
   }
   if (register_negotiations(client) < 0) {
      goto err;
   }
   if (telnet_create_connection(client, addr, port) < 0) {
      goto err;
   }
   if (start(client) < 0) {
      goto err;
   } 
   return;

err:
   cleanup_handlers(client);
   perror(NULL);
   exit(EXIT_FAILURE);
}

static int handle_client(TelnetClient *client) {
   unsigned char buff[BUFSIZ];
   int result;
   size_t ret, amt;
   result = telnet_receive(client, buff, sizeof(buff), &amt);
   if (result < 0) {
      return -1;
   }
   if (!result) {
      return 0;
   }
   if (amt == 0) {
      return 1;
   }
   ret = safe_write(STDOUT_FILENO, buff, amt);
   if (ret != amt) {
      return -1;
   }
   return 1;
}

static int do_environ_list_command(void) {
   const char *env, *value;
   int ret = 1;
   EnvVarItr *iterator = init_env_var_itr();
   if (iterator == NULL) {
      return -1;
   }
   while (env_itr(iterator, &env, &value)) {
      printf("%s: %s\n", env, value);
   }
   free_env_var_itr(iterator);
   return ret;
}

static int do_environ_command(char *args) {
   int ret = 1;
   size_t arg1_len = strcspn(args, " ");
   if (arg1_len == 0) {
      printf("missing arguments for environ\n");
      return 0;
   }
   args[arg1_len] = '\0';
   if (strcmp(args, "list") == 0) {
      ret = do_environ_list_command();
   } else {
      printf("unkown environ action\n");
   }
   return ret;
}

static int do_command(char *buf) {
   int ret = 1;
   size_t cmd_len = strcspn(buf, " ");
   buf[cmd_len] = '\0';
   if (strcmp(buf, "environ") == 0) {
      ret = do_environ_command(buf + (cmd_len + 1));
   } else if (strcmp(buf, "close") == 0){
      ret = 0; 
   } else {
      printf("unkown command\n");
   }
   return ret;
}

static int command_mode(TelnetClient *client) {
   char buf[BUFSIZ];
   int result;
   size_t command_len;
   if (tty_default_mode(&term_state) < 0) {
      return -1;
   } 
   printf("\ntelnet> ");
   if (fgets(buf, sizeof(buf), stdin) == NULL) {
      return -1;
   }
   command_len = strcspn(buf, "\n");
   if (command_len == 0) {
      if (tty_reset_last_state(&term_state) < 0) {
         return -1;
      }
      return 1;
   }
   buf[command_len] = '\0';
   result = do_command(buf);
   if (result < 0) {
      return -1;
   }
   if (!result) {
      return 0;
   }
   if (tty_reset_last_state(&term_state) < 0) {
      return -1;
   }
   return 1;
}

static int handle_in(TelnetClient *client) {
   char buff[BUFSIZ];
   ssize_t amt;
   size_t i;
   int ret = 1;
   amt = read(STDIN_FILENO, buff, sizeof(buff));
   if (amt < 0) {
      return -1;
   }
   for (i = 0; i < amt && ret >= 0; ++i) {
      if (buff[i] == 29) {
	 ret = command_mode(client);
	 if (ret == 0) {
	    break;
	 }
      }
      telnet_send(client, (unsigned char *)(buff + i), 1);
   }
   return ret;
}

static int register_negotiations(TelnetClient *client) {
   struct opt_handler *handler;
   /*register echo handler*/
   if (terminal_state_set) {
      handler = new_echo_opt_handler(&term_state);
   } else {
      handler = new_echo_opt_handler(NULL);
   }
   if (handler == NULL) {
      return -1;
   }
   if (telnet_reg_opt_handler(client, handler) < 0) {
      return -1;
   }

   /*register naws handler*/
   handler = new_naws_opt_handler(sigwinch_handler);
   if (handler == NULL) {
      return -1;
   }
   if (telnet_reg_opt_handler(client, handler) < 0) {
      return -1;
   }

   /*resgister termtype handler*/
   handler = new_termtype_opt_handler();
   if (handler == NULL) {
      return -1;
   }
   if (telnet_reg_opt_handler(client, handler) < 0) {
      return -1;
   }
   return 0;
}

static void term_atexit(void) {
   tty_reset(&term_state);
}

static int find_tty_fd(int fd1, int fd2) {
   if (isatty(fd1)) {
      return fd1;
   }
   if (isatty(fd2)) {
      return fd2;
   }
   return -1;
}

static int setup_terminal(int intermfd, int outtermfd) {
   int ttyfd;
   if ((ttyfd = find_tty_fd(intermfd, outtermfd)) < 0) {
      return 0;
   }
   if (register_tty(ttyfd, &term_state) < 0) {
      return -1;
   }
   if (atexit(term_atexit) < 0) {
      return -1;
   }
   if (tty_default_mode(&term_state) < 0) {
      return -1;
   }
   terminal_state_set = 1;
   return 0;
}

