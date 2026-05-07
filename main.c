#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/wait.h>

#define PORT 8080
#define BUF_SIZE 16384
#define MAX_HEADERS 64
#define MAX_HEADER_LEN 1024

typedef struct {
    char buf[BUF_SIZE];
    int start;
    int end;
} stream_buf_t;

static int fill_buf(int sock, stream_buf_t *stream_buf) {
    if (stream_buf->start > 0) {
        if (stream_buf->start < stream_buf->end)
            memmove(stream_buf->buf, stream_buf->buf + stream_buf->start, stream_buf->end - stream_buf->start);
        stream_buf->end -= stream_buf->start;
        stream_buf->start = 0;
    }

    int n = recv(sock, stream_buf->buf + stream_buf->end, BUF_SIZE - stream_buf->end, 0);
    if (n <= 0) return n;

    stream_buf->end += n;
    return n;
}

static int get_line(int sock, stream_buf_t *stream_buf, char *out, int size) {
    int i = 0;

    while (true) {
        if (stream_buf->start >= stream_buf->end) {
            int n = fill_buf(sock, stream_buf);
            if (n <= 0) return n;
        }
        char c = stream_buf->buf[stream_buf->start++];
        if (i < size - 1) out[i++] = c;
        if (c == '\n') break;
    }
    out[i] = '\0';
    return i;
}

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

int build_httpbin(const char *method, const char *path,
                  char headers[][MAX_HEADER_LEN], int header_count,
                  char *out, int maxlen, const char *client_ip) {
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
    len += snprintf(out + len, maxlen - len, "  \"url\": \"%s\"\n", path);
    len += snprintf(out + len, maxlen - len, "}\n");

    return len;
}


int build_httpbin_post(const char *method, const char *path,
                       char headers[][MAX_HEADER_LEN], int header_count,
                       const char *body, int body_len,
                       char *out, int maxlen, const char *client_ip) {
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
    len += snprintf(out + len, maxlen - len, "  \"url\": \"%s\"\n", path);
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

int main(int argc, char *argv[]) {
    int port = PORT;
    
    if (argc > 1) {
        port = atoi(argv[1]);
    }
    
    int listenfd, connfd;
    struct sockaddr_in servaddr;
    stream_buf_t stream_buf;
    stream_buf.start = 0;
    stream_buf.end = 0;
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
    while (true) {
        connfd = accept(listenfd, NULL, NULL);
        if (connfd < 0) continue;

        // request line
        char req_line[MAX_HEADER_LEN];
        int n = get_line(connfd, &stream_buf, req_line, sizeof(req_line));
        if (n <= 0) {
            close(connfd);
            continue;
        }

        // method and path
        char method[32] = {0}, path[1024] = {0}, proto[32] = {0};
        sscanf(req_line, "%31s %1023s %31s", method, path, proto);
        get_path(req_line, path, sizeof(path));

        // header
        char headers[MAX_HEADERS][MAX_HEADER_LEN];
        int header_count = 0;
        int content_length = 0;

        while (header_count < MAX_HEADERS) {
            char line[MAX_HEADER_LEN];
            n = get_line(connfd, &stream_buf, line, sizeof(line));
            if (n <= 0) break;

            // empty line mark
            if (n == 1 && line[0] == '\n') break;
            if (n == 2 && strcmp(line, "\r\n") == 0) break;

            // trim trailing \r\n
            int len = n;
            if (len >= 2 && line[len-2] == '\r') len -= 2;
            else if (len >= 1 && line[len-1] == '\n') len--;
            line[len] = '\0';

            // Content-Length
            if (strncasecmp(line, "Content-Length:", 15) == 0) {
                content_length = atoi(line + 15);
            }

            // Store header line
            if (len > 0 && header_count < MAX_HEADERS) {
                strncpy(headers[header_count], line, MAX_HEADER_LEN - 1);
                headers[header_count][MAX_HEADER_LEN - 1] = '\0';
                header_count++;
            }
        }

        // body
        char body[BUF_SIZE] = {0};
        int body_len = 0;
        if (content_length > 0 && content_length < BUF_SIZE) {
            while (body_len < content_length) {
                n = read(connfd, body + body_len, content_length - body_len);
                if (n > 0) {
                    body_len += n;
                } else {
                    break;
                }
            }
        }

        // client IP
        struct sockaddr_in addr;
        socklen_t addr_len = sizeof(addr);
        char ip[64] = "unknown";
        if (getpeername(connfd, (struct sockaddr *) &addr, &addr_len) == 0) {
            inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
        }

        char resp_body[BUF_SIZE];
        int resp_body_len = 0;
        const char *ctype = "text/plain";

        if (path_match(path, "/get")) {
            resp_body_len = build_httpbin(method, path, headers, header_count, resp_body, sizeof(resp_body), ip);
            ctype = "application/json";
        } else if (path_match(path, "/post")) {
            resp_body_len = build_httpbin_post(method, path, headers, header_count, body, body_len, resp_body, sizeof(resp_body), ip);
            ctype = "application/json";
        } else if (path_match(path, "/raw")) {
            resp_body_len = snprintf(resp_body, sizeof(resp_body), "%s %s %s\n", method, path, proto);
            for (int i = 0; i < header_count; i++) {
                resp_body_len += snprintf(resp_body + resp_body_len, sizeof(resp_body) - resp_body_len, "%s\n", headers[i]);
            }
        } else if (strcmp(path, "/") == 0) {
            resp_body_len = snprintf(resp_body, sizeof(resp_body), "{}\n");
        } else if (strncmp(path, "/exec/", 6) == 0) {
            resp_body_len = handle_exec(path, method, proto, headers, header_count, body, body_len, resp_body, sizeof(resp_body));
        } else {
            resp_body_len = snprintf(resp_body, sizeof(resp_body), "Not Found\n");
        }

        if (resp_body_len < 0) resp_body_len = 0;
        if (resp_body_len > BUF_SIZE) resp_body_len = BUF_SIZE;

        char resp[BUF_SIZE];
        int resp_len = snprintf(resp, sizeof(resp),
                                 "HTTP/1.1 200 OK\r\n"
                                 "Content-Length: %d\r\n"
                                 "Content-Type: %s\r\n"
                                 "Connection: close\r\n"
                                 "\r\n",
                                 resp_body_len, ctype);

        write(connfd, resp, resp_len);
        write(connfd, resp_body, resp_body_len);

        close(connfd);
    }
}
