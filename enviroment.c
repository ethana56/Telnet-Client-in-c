#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "enviroment.h"

extern char **environ;

struct env_var {
   char *env;
   char *value;
   int will_export;
   struct env_var *next;
};

struct env_var_itr {
   struct env_var *cur;
};

static struct env_var *head = NULL;

static void free_env_var(struct env_var *env_var) {
   free(env_var->env);
   free(env_var->value);
   free(env_var);
}

static void free_list(void) {
   struct env_var *cur;
   for (cur = head; cur; cur = cur->next) {
      free_env_var(cur);
   }
}

static struct env_var *make_env_var(char *oenv) {
   struct env_var *env_var;
   char *env_cpy, *env, *value;
   env_cpy = malloc(strlen(oenv) + 1);
   if (env_cpy == NULL) {
      return NULL;
   }
   strcpy(env_cpy, oenv);
   env = strtok(env_cpy, "=");
   value = strtok(NULL, "=");
   env_var = calloc(1, sizeof(struct env_var));
   if (env_var == NULL) {
      free(env_cpy);
      return NULL;
   }
   env_var->env = malloc(strlen(env) + 1);
   if (env_var->env == NULL) {
      goto err;
   }
   strcpy(env_var->env, env);
   env_var->value = malloc(strlen(value) + 1);
   if (env_var->value == NULL) {
      goto err;
   }
   strcpy(env_var->value, value);
   return env_var;

err:
   free(env_cpy);
   free_env_var(env_var);
   return NULL;
}

static int init_env(void) {
   char **env = environ;
   struct env_var **pp = &head;
   struct env_var *cur_var;
   while (*env) {
      cur_var = make_env_var(*env);
      if (cur_var == NULL) return -1;
      *pp = cur_var;
      pp = &cur_var->next;
      ++env;
   }
   return 0;   
}

static struct env_var *search_env_var(char *env) {
   struct env_var *cur;
   struct env_var *ret = NULL;
   for (cur = head; cur; cur = cur->next) {
      if (strcmp(cur->env, env) == 0) {
         ret = cur;
	 break;
      }
   }
   return ret;
}

EnvVarItr *init_env_var_itr(void) {
   EnvVarItr *itr = malloc(sizeof(EnvVarItr));
   if (itr == NULL) {
      return NULL;
   }
   itr->cur = head;
   return itr;
}

void free_env_var_itr(EnvVarItr *itr) {
   free(itr);
}

int env_itr(EnvVarItr *itr, const char **name, const char **value) {
   if (itr->cur == NULL) {
      return 0;
   }
   *name = itr->cur->env;
   *value = itr->cur->value;
   itr->cur = itr->cur->next;
   return 1;
}

char *get_env(char *env) {
   struct env_var *env_var;
   char *ret = NULL;
   if (head == NULL) {
      if (init_env() < 0) return NULL;
   }
   env_var = search_env_var(env);
   if (env_var != NULL) {
      ret = env_var->value;
   }
   return ret;
}
