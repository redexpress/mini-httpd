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

#include "common.h"
/*
#define PORT 8080
#define BUF_SIZE 16384
#define MAX_HEADERS 64
#define MAX_HEADER_LEN 1024

typedef struct {
    char buf[BUF_SIZE];
    int start;
    int end;
} stream_buf_t;
*/
#include "handler.h"

static int fill_buf(int sock, stream_buf_t *stream_buf) {
    if (stream_buf->start > 0) {
        if (stream_buf->start < stream_buf->end)
            memmove(stream_buf->buf, stream_buf->buf + stream_buf->start, stream_buf->end - stream_buf->start);
        stream_buf->end -= stream_buf->start;
        stream_buf->start = 0;
    }

    if (stream_buf->end >= BUF_SIZE) return -1;

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

        stream_buf_t stream_buf;
        stream_buf.start = 0;
        stream_buf.end = 0;
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
            int remain = stream_buf.end - stream_buf.start;
            if (remain > 0) {
                int copy = remain;
                if (copy > content_length) copy = content_length;
                memcpy(body, stream_buf.buf + stream_buf.start, copy);
                stream_buf.start += copy;
                body_len += copy;
            }
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

        // local addr for URL - prefer Host header, fallback to local socket
        const char *host_header = get_host_header(headers, header_count);
        char local_host[256] = "localhost";
        if (host_header && host_header[0]) {
            snprintf(local_host, sizeof(local_host), "%s", host_header);
        } else {
            struct sockaddr_in local_addr;
            socklen_t local_len = sizeof(local_addr);
            if (getsockname(connfd, (struct sockaddr *) &local_addr, &local_len) == 0) {
                char *p = local_host;
                int len = 0;
                inet_ntop(AF_INET, &local_addr.sin_addr, p, 256 - len);
                len = strlen(p);
                snprintf(p + len, 256 - len, ":%d", ntohs(local_addr.sin_port));
            }
        }

        char resp_body[BUF_SIZE];
        int resp_body_len = 0;
        const char *ctype = "text/plain";

        if (path_match(path, "/get")) {
            resp_body_len = build_httpbin(method, path, headers, header_count, resp_body, sizeof(resp_body), ip, local_host);
            ctype = "application/json";
        } else if (path_match(path, "/post")) {
            resp_body_len = build_httpbin_post(method, path, headers, header_count, body, body_len, resp_body, sizeof(resp_body), ip, local_host);
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