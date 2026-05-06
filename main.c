#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/wait.h>

#define PORT 8080
#define BUF_SIZE 16384

static void get_path(const char *buf, char *path, size_t n) {
    const char *p = buf;
    while (*p && *p != ' ') p++;
    if (!*p) {
        path[0] = '\0';
        return;
    }
    p++;
    size_t i = 0;
    while (*p && *p != ' ' && *p != '\r' && *p != '\n' && i < n - 1) {
        path[i++] = *p++;
    }
    path[i] = '\0';
}

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
    int first = 1;
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
        first = 0;
        len += snprintf(out + len, maxlen - len, "\"%s\": \"%s\"", key, val);
        if (*q == '&') q++;
    }
    len += snprintf(out + len, maxlen - len, "}");
    return len;
}

int build_get(char *req, int header_end, char *out, int maxlen, const char *client_ip) {
    char method[16], path[256], proto[16];
    sscanf(req, "%15s %255s %15s", method, path, proto);
    char args_buf[512];
    build_args_from_path(path, args_buf, sizeof(args_buf));
    int len = 0;
    len += snprintf(out + len, maxlen - len, "{\n");
    len += snprintf(out + len, maxlen - len, "  \"args\": %s,\n", args_buf);
    len += snprintf(out + len, maxlen - len, "  \"headers\": {\n");
    char *p = strstr(req, "\r\n");
    if (!p) return 0;
    p += 2;
    int first = 1;
    while (p && p < req + header_end - 2) {
        char *e = strstr(p, "\r\n");
        if (!e) break;
        char tmp = *e;
        *e = '\0';
        char *c = strchr(p, ':');
        if (c) {
            *c = '\0';
            char *key = p;
            char *val = c + 1;
            while (*val == ' ') val++;
            if (!first) len += snprintf(out + len, maxlen - len, ",\n");
            first = 0;
            len += snprintf(out + len, maxlen - len, "    \"%s\": \"%s\"", key, val);
            *c = ':';
        }
        *e = tmp;
        p = e + 2;
    }

    len += snprintf(out + len, maxlen - len, "\n  },\n");
    len += snprintf(out + len, maxlen - len, "  \"origin\": \"%s\",\n", client_ip ? client_ip : "unknown");
    len += snprintf(out + len, maxlen - len, "  \"url\": \"%s\"\n", path);
    len += snprintf(out + len, maxlen - len, "}\n");

    return len;
}


int build_post(char *req, int header_end, char *out, int maxlen, const char *client_ip) {
    char method[16], path[256], proto[16];
    sscanf(req, "%15s %255s %15s", method, path, proto);
    char *body = req + header_end;
    char args_buf[512];
    build_args_from_path(path, args_buf, sizeof(args_buf));
    int len = 0;
    len += snprintf(out + len, maxlen - len, "{\n");
    len += snprintf(out + len, maxlen - len, "  \"args\": %s,\n", args_buf);

    int is_json = 0;
    char *ctype = strcasestr(req, "Content-Type:");
    if (ctype && strstr(ctype, "application/json")) {
        is_json = 1;
    }

    if (is_json) {
        len += snprintf(out + len, maxlen - len, "  \"data\": \"%s\",\n", body ? body : "");
    } else {
        len += snprintf(out + len, maxlen - len, "  \"data\": \"\",\n");
    }

    len += snprintf(out + len, maxlen - len, "  \"files\": {},\n");
    len += snprintf(out + len, maxlen - len, "  \"form\": {\n");

    if (!is_json && body && *body) {
        len += snprintf(out + len, maxlen - len, "    \"%s\": \"\"\n", body);
    }

    len += snprintf(out + len, maxlen - len, "  },\n");
    len += snprintf(out + len, maxlen - len, "  \"headers\": {\n");

    char *p = strstr(req, "\r\n");
    if (!p) return 0;
    p += 2;

    int first = 1;

    while (p && p < req + header_end - 2) {
        char *e = strstr(p, "\r\n");
        if (!e) break;

        char tmp = *e;
        *e = '\0';

        char *c = strchr(p, ':');
        if (c) {
            *c = '\0';
            char *key = p;
            char *val = c + 1;
            while (*val == ' ') val++;
            if (!first) len += snprintf(out + len, maxlen - len, ",\n");
            first = 0;
            len += snprintf(out + len, maxlen - len, "    \"%s\": \"%s\"", key, val);
            *c = ':';
        }

        *e = tmp;
        p = e + 2;
    }

    len += snprintf(out + len, maxlen - len, "\n  },\n");

    if (is_json) {
        len += snprintf(out + len, maxlen - len, "  \"json\": %s,\n", body ? body : "null");
    } else {
        len += snprintf(out + len, maxlen - len, "  \"json\": null,\n");
    }

    len += snprintf(out + len, maxlen - len, "  \"origin\": \"%s\",\n", client_ip ? client_ip : "unknown");
    len += snprintf(out + len, maxlen - len, "  \"url\": \"%s\"\n", path);
    len += snprintf(out + len, maxlen - len, "}\n");

    return len;
}

int path_match(const char *path, const char *route) {
    int n = strlen(route);
    return strncmp(path, route, n) == 0 &&
           (path[n] == '\0' || path[n] == '?');
}

int build_script_input(char *req, int header_end, char *out, int maxlen) {
    int len = 0;

    char method[16], path[256], proto[16];
    sscanf(req, "%15s %255s %15s", method, path, proto);

    len += snprintf(out + len, maxlen - len, "__method: %s\n", method);
    len += snprintf(out + len, maxlen - len, "__path: %s\n", path);
    len += snprintf(out + len, maxlen - len, "__proto: %s\n", proto);
    // headers
    char *p = strstr(req, "\r\n");
    if (p) p += 2;

    while (p && p < req + header_end - 2) {
        char *e = strstr(p, "\r\n");
        if (!e) break;
        int l = e - p;
        if (l <= 0) break;
        char line[512];
        if (l >= (int) sizeof(line)) l = sizeof(line) - 1;
        memcpy(line, p, l);
        line[l] = '\0';
        len += snprintf(out + len, maxlen - len, "%s\n", line);
        p = e + 2;
    }
    // body
    char *body = req + header_end;
    len += snprintf(out + len, maxlen - len, "__body: ");
    if (body) {
        int blen = strlen(body);
        if (len + blen < maxlen) {
            memcpy(out + len, body, blen);
            len += blen;
        }
    }

    len += snprintf(out + len, maxlen - len, "\n");
    return len;
}

int handle_exec(char *path, char *req, int header_end, char *body, int maxlen) {
    char *cmd = path + 6;
    if (!*cmd) {
        return snprintf(body, maxlen, "exec: empty\n");
    }
    char script[512];
    snprintf(script, sizeof(script), "./%s.sh", cmd);
    char input[BUF_SIZE];
    int input_len = build_script_input(req, header_end, input, sizeof(input));

    int inpipe[2], outpipe[2];
    if (pipe(inpipe) < 0 || pipe(outpipe) < 0) {
        return snprintf(body, maxlen, "pipe error\n");
    }

    pid_t pid = fork();

    if (pid < 0) {
        return snprintf(body, maxlen, "fork error\n");
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
    while (1) {
        int n = read(outpipe[0], body + total, maxlen - total);
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
        return snprintf(body, maxlen, "exec failed\n");
    }

    return total;
}

int main(int argc, char *argv[]) {
    int port = PORT;
    
    if (argc > 1) {
        port = atoi(argv[1]);
    }
    
    int listenfd, connfd;
    struct sockaddr_in servaddr;
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(port);
    if (bind(listenfd, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0) {
        perror("bind");
        exit(1);
    }
    if (listen(listenfd, 128) < 0) {
        perror("listen");
        exit(1);
    }
    printf("server running on %d\n", port);
    while (1) {
        connfd = accept(listenfd, NULL, NULL);
        if (connfd < 0) continue;
        char buf[BUF_SIZE];
        int total = 0;
        int header_end = -1;
        int content_length = 0;
        while (1) {
            if (total >= BUF_SIZE - 1) break;
            int n = read(connfd, buf + total, BUF_SIZE - total - 1);
            if (n > 0) {
                total += n;
                buf[total] = '\0';
                if (header_end == -1) {
                    for (int i = 0; i < total - 3; i++) {
                        if (buf[i] == '\r' && buf[i + 1] == '\n' &&
                            buf[i + 2] == '\r' && buf[i + 3] == '\n') {
                            header_end = i + 4;
                            break;
                        }
                    }
                }

                if (header_end != -1 && content_length == 0) {
                    char *cl = strcasestr(buf, "Content-Length:");
                    if (cl) content_length = atoi(cl + 15);
                }

                if (header_end != -1) {
                    int expected = header_end + content_length;
                    if (total >= expected) break;
                }
            } else if (n == 0) {
                break;
            } else {
                if (errno == EINTR) continue;
                break;
            }
        }

        if (total <= 0) {
            close(connfd);
            continue;
        }

        char path[1024];
        get_path(buf, path, sizeof(path));

        char body[BUF_SIZE];
        int body_len = 0;

        if (path_match(path, "/get")) {
            struct sockaddr_in addr;
            socklen_t len = sizeof(addr);

            if (getpeername(connfd, (struct sockaddr *) &addr, &len) == 0) {
                char ip[64];
                inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
                body_len = build_get(buf, header_end, body, sizeof(body), ip);
            } else {
                body_len = snprintf(body, sizeof(body), "{\"error\":\"getpeername failed\"}\n");
            }
        } else if (path_match(path, "/post")) {
            struct sockaddr_in addr;
            socklen_t len = sizeof(addr);
            getpeername(connfd, (struct sockaddr *) &addr, &len);
            char ip[64];
            inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
            body_len = build_post(buf, header_end, body, sizeof(body), ip);
        } else if (path_match(path, "/raw")) {
            body_len = build_raw_text(buf, total, body, sizeof(body));
        } else if (strcmp(path, "/") == 0) {
            body_len = snprintf(body, sizeof(body), "{}\n");
        } else if (strncmp(path, "/exec/", 6) == 0) {
            body_len = handle_exec(path, buf, header_end, body, sizeof(body));
        } else {
            body_len = build_raw_text(buf, total, body, sizeof(body));
        }

        if (body_len < 0) body_len = 0;
        if (body_len > BUF_SIZE) body_len = BUF_SIZE;

        char resp[BUF_SIZE];
        const char *ctype = "text/plain";
        if (path_match(path, "/get") || path_match(path, "/post")) {
            ctype = "application/json";
        }
        int len = snprintf(resp, sizeof(resp),
                           "HTTP/1.1 200 OK\r\n"
                           "Content-Length: %d\r\n"
                           "Content-Type: %s\r\n"
                           "Connection: close\r\n"
                           "\r\n",
                           body_len, ctype);

        write(connfd, resp, len);

        int sent = 0;
        while (sent < body_len) {
            int n = write(connfd, body + sent, body_len - sent);
            if (n > 0) sent += n;
            else break;
        }

        close(connfd);
    }
}
