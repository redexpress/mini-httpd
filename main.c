#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/wait.h>

#include "common.h"
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

static void send_400(int connfd) {
    const char *resp = "HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n";
    write(connfd, resp, strlen(resp));
}

void log_access(int connfd, const char *method, const char *path, const char *proto) {
    time_t now = time(NULL);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now));

    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    char ip[64] = "unknown";
    if (getpeername(connfd, (struct sockaddr *) &addr, &addr_len) == 0) {
        inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
    }
    printf("[%s] \033[32m%s\033[0m - %s \033[36m%s\033[0m %s\n", time_str, ip, method, path, proto);
    fflush(stdout);
}

void send_chunk(int connfd, const char *data, int len) {
    if (len <= 0) return;
    char chunk_header[32];
    int header_len = snprintf(chunk_header, sizeof(chunk_header), "%x\r\n", len);
    write(connfd, chunk_header, header_len);
    write(connfd, data, len);
    write(connfd, "\r\n", 2);
}

void send_chunk_end(int connfd) {
    write(connfd, "0\r\n\r\n", 5);
}

void handle_time_chunked(int connfd) {
    for (int i = 0; i < 10; i++) {
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        char time_buf[128];

        int len = snprintf(time_buf, sizeof(time_buf),
                           "<div>Current Server Time: %04d-%02d-%02d %02d:%02d:%02d</div>\n",
                           tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
                           tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);

        send_chunk(connfd, time_buf, len);
        sleep(1);
    }
}

int main(int argc, char *argv[]) {
    int port = PORT;

    if (argc > 1) {
        port = atoi(argv[1]);
    }

    signal(SIGPIPE, SIG_IGN);  // Ignore this to prevent the server from crashing if it writes to a closed socket.
    signal(SIGCHLD, SIG_IGN);  // Set to SIG_IGN so the kernel automatically reaps child processes, preventing "zombie" processes without needing explicit waitpid() calls.

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
        perror("bind failed");
        exit(1);
    }
    if (listen(listenfd, 128) < 0) {
        perror("listen failed");
        exit(1);
    }
    printf("server running on %d\n", port);
    while (true) {
        connfd = accept(listenfd, NULL, NULL);
        if (connfd < 0) {
            if (errno == EINTR) continue; // Retry immediately if interrupted by a signal (e.g., SIGCHLD)
            continue;                     // Skip current iteration for other system errors (e.g., EMFILE). kept separate for future extensibility
        }

        pid_t pid = fork();

        if (pid < 0) {
            perror("fork failed");
            close(connfd);
            continue;
        }
        if (pid > 0) {
            close(connfd);
            continue;
        }
        close(listenfd);
        stream_buf_t stream_buf;
        stream_buf.start = 0;
        stream_buf.end = 0;
        // request line
        char req_line[MAX_HEADER_LEN];
        int n = get_line(connfd, &stream_buf, req_line, sizeof(req_line));
        if (n <= 0) {
            send_400(connfd);
            close(connfd);
            exit(0);
        }

        char method[32] = {0}, path[1024] = {0}, proto[32] = {0};
        sscanf(req_line, "%31s %1023s %31s", method, path, proto);
        log_access(connfd, method, path, proto);

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
                int copy = (remain > content_length) ? content_length : remain;
                memcpy(body, stream_buf.buf + stream_buf.start, copy);
                stream_buf.start += copy;
                body_len += copy;
            }
            while (body_len < content_length) {
                n = read(connfd, body + body_len, content_length - body_len);
                if (n > 0) {
                    body_len += n;
                } else if (n == 0) {
                    break;
                } else {
                    if (errno == EINTR) continue;
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
        }
        if (path_match(path, "/time")) {
            // Chunked Response Path
            const char *header =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/html; charset=utf-8\r\n"
                "Transfer-Encoding: chunked\r\n"
                "X-Content-Type-Options: nosniff\r\n"
                "Cache-Control: no-cache, no-store, must-revalidate\r\n"
                "Connection: close\r\n"
                "\r\n";
            write(connfd, header, strlen(header));

            handle_time_chunked(connfd);
            send_chunk_end(connfd);
            close(connfd);
            exit(0);
        } else {
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
            } else if (strncmp(path, "/dir", 4) == 0) {
                resp_body_len = handle_static(path, resp_body, sizeof(resp_body), &ctype);
            } else if (strncmp(path, "/status/", 8) == 0) {
                handle_status(path, connfd);
                close(connfd);
                exit(0);
            } else if (strcmp(path, "/favicon.ico") == 0) {
                const char *resp = "HTTP/1.1 204 No Content\r\nConnection: close\r\n\r\n";
                write(connfd, resp, strlen(resp));
                close(connfd);
                exit(0);
            } else {
                resp_body_len = snprintf(resp_body, sizeof(resp_body), "%s %s %s\n", method, path, proto);
                for (int i = 0; i < header_count; i++) {
                    resp_body_len += snprintf(resp_body + resp_body_len, sizeof(resp_body) - resp_body_len, "%s\n", headers[i]);
                }
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
            exit(0);
        }
    }
}