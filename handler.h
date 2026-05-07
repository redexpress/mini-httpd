#ifndef HANDLER_H
#define HANDLER_H

#include <stdbool.h>
#include "common.h"

int build_raw_text(char *req, int len, char *out, int maxlen);
int build_args_from_path(const char *path, char *out, int maxlen);
int build_httpbin(const char *method, const char *path,
                 char headers[][MAX_HEADER_LEN], int header_count,
                 char *out, int maxlen, const char *client_ip,
                 const char *host);
int build_httpbin_post(const char *method, const char *path,
                      char headers[][MAX_HEADER_LEN], int header_count,
                      const char *body, int body_len,
                      char *out, int maxlen, const char *client_ip,
                      const char *host);
bool path_match(const char *path, const char *route);
int build_script_input(const char *method, const char *path, const char *proto,
                       char headers[][MAX_HEADER_LEN], int header_count,
                       const char *body, int body_len,
                       char *out, int maxlen);
int handle_exec(const char *path, const char *method, const char *proto,
               char headers[][MAX_HEADER_LEN], int header_count,
               const char *body, int body_len,
               char *body_out, int maxlen);

#endif