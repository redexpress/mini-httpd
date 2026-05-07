#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#include "handler.h"

int build_raw_text(char *req, int len, char *out, int maxlen) {
    if (len > maxlen - 1) len = maxlen - 1;
    memcpy(out, req, len);
    out[len] = '\0';
    return len;
}

int build_args_from_path(const char *path, char *out, int maxlen) {
    const char *q = strchr(path, '?');
    int len = 0;
    len += snprintf(out + len, maxlen - len, "{");
    if (!q || *(q + 1) == '\0') {
        len += snprintf(out + len, maxlen - len, "}");
        return len;
    }
    q++;
    bool first = true;
    while (*q) {
        char key[256] = {0};
        char val[256] = {0};
        int i = 0;
        while (*q && *q != '=' && *q != '&' && i < 255) {
            key[i++] = *q++;
        }
        key[i] = '\0';
        if (*q == '=') {
            q++;
            i = 0;
            while (*q && *q != '&' && i < 255) {
                val[i++] = *q++;
            }
            val[i] = '\0';
        }
        if (!first) len += snprintf(out + len, maxlen - len, ", ");
        first = false;
        len += snprintf(out + len, maxlen - len, "\"%s\": \"%s\"", key, val);
        if (*q == '&') q++;
    }
    len += snprintf(out + len, maxlen - len, "}");
    return len;
}

static bool parse_header_line(const char *line, char *key, char *val) {
    const char *c = strchr(line, ':');
    if (!c) return false;
    size_t key_len = c - line;
    if (key_len > 255) key_len = 255;
    memcpy(key, line, key_len);
    key[key_len] = '\0';

    c++;
    while (*c == ' ') c++;
    strncpy(val, c, 255);
    val[255] = '\0';
    return true;
}

static const char *get_host_header(char headers[][MAX_HEADER_LEN], int header_count) {
    for (int i = 0; i < header_count; i++) {
        if (strncasecmp(headers[i], "Host:", 5) == 0) {
            const char *p = headers[i] + 5;
            while (*p == ' ') p++;
            return p;
        }
    }
    return "localhost";
}

int build_httpbin(const char *method, const char *path,
                 char headers[][MAX_HEADER_LEN], int header_count,
                 char *out, int maxlen, const char *client_ip,
                 const char *host) {
    char args_buf[512];
    build_args_from_path(path, args_buf, sizeof(args_buf));
    int len = 0;
    len += snprintf(out + len, maxlen - len, "{\n");
    len += snprintf(out + len, maxlen - len, "  \"args\": %s,\n", args_buf);
    len += snprintf(out + len, maxlen - len, "  \"headers\": {\n");

    bool first = true;
    for (int i = 0; i < header_count; i++) {
        char key[256] = {0}, val[256] = {0};
        if (parse_header_line(headers[i], key, val)) {
            if (!first) len += snprintf(out + len, maxlen - len, ",\n");
            first = false;
            len += snprintf(out + len, maxlen - len, "    \"%s\": \"%s\"", key, val);
        }
    }

    len += snprintf(out + len, maxlen - len, "\n  },\n");
    len += snprintf(out + len, maxlen - len, "  \"origin\": \"%s\",\n", client_ip ? client_ip : "unknown");
    len += snprintf(out + len, maxlen - len, "  \"url\": \"http://%s%s\"\n", host, path);
    len += snprintf(out + len, maxlen - len, "}\n");

    return len;
}

int build_httpbin_post(const char *method, const char *path,
                      char headers[][MAX_HEADER_LEN], int header_count,
                      const char *body, int body_len,
                      char *out, int maxlen, const char *client_ip,
                      const char *host) {
    char args_buf[512];
    build_args_from_path(path, args_buf, sizeof(args_buf));
    int len = 0;
    len += snprintf(out + len, maxlen - len, "{\n");
    len += snprintf(out + len, maxlen - len, "  \"args\": %s,\n", args_buf);

    bool is_json = false;
    for (int i = 0; i < header_count; i++) {
        char key[256] = {0}, val[256] = {0};
        if (parse_header_line(headers[i], key, val)) {
            if (strcasecmp(key, "Content-Type") == 0) {
                if (strstr(val, "application/json")) {
                    is_json = true;
                }
            }
        }
    }

    if (is_json && body && body_len > 0) {
        len += snprintf(out + len, maxlen - len, "  \"data\": \"%.*s\",\n", body_len, body);
    } else {
        len += snprintf(out + len, maxlen - len, "  \"data\": \"\",\n");
    }

    len += snprintf(out + len, maxlen - len, "  \"files\": {},\n");
    len += snprintf(out + len, maxlen - len, "  \"form\": {\n");

    if (!is_json && body && body_len > 0) {
        len += snprintf(out + len, maxlen - len, "    \"%.*s\": \"\"\n", body_len, body);
    }

    len += snprintf(out + len, maxlen - len, "  },\n");
    len += snprintf(out + len, maxlen - len, "  \"headers\": {\n");

    bool first = true;
    for (int i = 0; i < header_count; i++) {
        char key[256] = {0}, val[256] = {0};
        if (parse_header_line(headers[i], key, val)) {
            if (!first) len += snprintf(out + len, maxlen - len, ",\n");
            first = false;
            len += snprintf(out + len, maxlen - len, "    \"%s\": \"%s\"", key, val);
        }
    }

    len += snprintf(out + len, maxlen - len, "\n  },\n");

    if (is_json && body && body_len > 0) {
        len += snprintf(out + len, maxlen - len, "  \"json\": %.*s,\n", body_len, body);
    } else {
        len += snprintf(out + len, maxlen - len, "  \"json\": null,\n");
    }

    len += snprintf(out + len, maxlen - len, "  \"origin\": \"%s\",\n", client_ip ? client_ip : "unknown");
    len += snprintf(out + len, maxlen - len, "  \"url\": \"http://%s%s\"\n", host, path);
    len += snprintf(out + len, maxlen - len, "}\n");

    return len;
}

bool path_match(const char *path, const char *route) {
    size_t n = strlen(route);
    return strncmp(path, route, n) == 0 &&
           (path[n] == '\0' || path[n] == '?');
}

int build_script_input(const char *method, const char *path, const char *proto,
                       char headers[][MAX_HEADER_LEN], int header_count,
                       const char *body, int body_len,
                       char *out, int maxlen) {
    int len = 0;
    len += snprintf(out + len, maxlen - len, "__method: %s\n", method);
    len += snprintf(out + len, maxlen - len, "__path: %s\n", path);
    len += snprintf(out + len, maxlen - len, "__proto: %s\n", proto);

    for (int i = 0; i < header_count; i++) {
        len += snprintf(out + len, maxlen - len, "%s\n", headers[i]);
    }

    len += snprintf(out + len, maxlen - len, "__body: ");
    if (body && body_len > 0) {
        if (len + body_len < maxlen) {
            memcpy(out + len, body, body_len);
            len += body_len;
        }
    }
    len += snprintf(out + len, maxlen - len, "\n");

    return len;
}

int handle_exec(const char *path, const char *method, const char *proto,
               char headers[][MAX_HEADER_LEN], int header_count,
               const char *body, int body_len,
               char *body_out, int maxlen) {
    const char *cmd = path + 6;
    if (!*cmd) {
        return snprintf(body_out, maxlen, "exec: empty\n");
    }
    char script[512];
    snprintf(script, sizeof(script), "./%s.sh", cmd);
    char input[BUF_SIZE];
    int input_len = build_script_input(method, path, proto, headers, header_count, body, body_len, input, sizeof(input));

    int inpipe[2], outpipe[2];
    if (pipe(inpipe) < 0 || pipe(outpipe) < 0) {
        return snprintf(body_out, maxlen, "pipe error\n");
    }

    pid_t pid = fork();

    if (pid < 0) {
        return snprintf(body_out, maxlen, "fork error\n");
    }

    if (pid == 0) {
        dup2(inpipe[0], STDIN_FILENO);
        dup2(outpipe[1], STDOUT_FILENO);

        close(inpipe[1]);
        close(outpipe[0]);
        execl("./busybox", "busybox", "ash", script, NULL);
        exit(1);
    }

    close(inpipe[0]);
    close(outpipe[1]);
    write(inpipe[1], input, input_len);
    close(inpipe[1]);
    int total = 0;
    while (true) {
        int n = read(outpipe[0], body_out + total, maxlen - total);
        if (n > 0) {
            total += n;
            if (total >= maxlen) break;
        } else {
            break;
        }
    }

    close(outpipe[0]);
    waitpid(pid, NULL, 0);

    if (total == 0) {
        return snprintf(body_out, maxlen, "exec failed\n");
    }

    return total;
}