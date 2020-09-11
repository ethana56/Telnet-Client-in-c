#ifndef ENVIROMENT_H
#define ENVIROMENT_H

struct env_var_itr;
typedef struct env_var_itr EnvVarItr;

EnvVarItr *init_env_var_itr(void);
void free_env_var_itr(EnvVarItr *);
int env_itr(EnvVarItr *itr, const char **, const char **);
char *get_env(char *);

#endif
