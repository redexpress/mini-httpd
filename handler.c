#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <dirent.h>

#include "handler.h"

static const char *mime_types[][2] = {
    {".html", "text/html"},
    {".htm", "text/html"},
    {".css", "text/css"},
    {".js", "application/javascript"},
    {".json", "application/json"},
    {".txt", "text/plain"},
    {".png", "image/png"},
    {".jpg", "image/jpeg"},
    {".gif", "image/gif"},
    {".svg", "image/svg+xml"},
    {".ico", "image/x-icon"},
    {".pdf", "application/pdf"},
    {".zip", "application/zip"},
    {".gz", "application/gzip"},
    {".tar", "application/x-tar"},
    {".mp4", "video/mp4"},
    {".mp3", "audio/mpeg"},
    {".wav", "audio/wav"},
    {NULL, "application/octet-stream"}
};

const char *get_mime_type(const char *path) {
    const char *ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";

    for (int i = 0; mime_types[i][0] != NULL; i++) {
        if (strcmp(ext, mime_types[i][0]) == 0) {
            return mime_types[i][1];
        }
    }
    return "application/octet-stream";
}

void build_dir_listing(const char *dir_path, const char *uri, char *out, int maxlen) {
    DIR *dir = opendir(dir_path);
    if (!dir) {
        snprintf(out, maxlen, "Cannot open directory: %s\n", dir_path);
        return;
    }

    int len = 0;
    len += snprintf(out + len, maxlen - len,
        "<!DOCTYPE HTML>\n"
        "<html><head><meta charset=\"utf-8\">"
        "<title>Directory: %s</title></head>\n"
        "<body><h1>Directory: %s</h1>\n"
        "<hr><ul>\n", uri, uri);

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0) continue;

        int is_dir = (entry->d_type == DT_DIR);
        char name[512];
        snprintf(name, sizeof(name), "%s", entry->d_name);

        char link[1024];
        if (is_dir) {
            snprintf(link, sizeof(link), "%s/", entry->d_name);
        } else {
            snprintf(link, sizeof(link), "%s", entry->d_name);
        }

        len += snprintf(out + len, maxlen - len,
            "<li><a href=\"%s%s\">%s%s</a></li>\n",
            uri, link, name, is_dir ? "/" : "");
    }

    closedir(dir);

    len += snprintf(out + len, maxlen - len,
        "</ul><hr></body></html>\n");
}

static void html_escape(const char *src, char *dst, int dst_size) {
    int j = 0;
    for (int i = 0; src[i] && j < dst_size - 1; i++) {
        switch (src[i]) {
            case '<': strcpy(dst + j, "&lt;"); j += 4; break;
            case '>': strcpy(dst + j, "&gt;"); j += 4; break;
            case '&': strcpy(dst + j, "&amp;"); j += 5; break;
            case '"': strcpy(dst + j, "&quot;"); j += 6; break;
            default: dst[j++] = src[i]; break;
        }
    }
    dst[j] = '\0';
}

int build_dir_listing_escape(const char *dir_path, const char *uri, char *out, int maxlen) {
    DIR *dir = opendir(dir_path);
    if (!dir) {
        return -1;
    }

    int len = 0;
    len += snprintf(out + len, maxlen - len,
        "<!DOCTYPE HTML>\n"
        "<html><head><meta charset=\"utf-8\">"
        "<title>Directory: %s</title></head>\n"
        "<body><h1>Directory: %s</h1>\n"
        "<hr><ul>\n", uri, uri);

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0) continue;

        int is_dir = (entry->d_type == DT_DIR);
        char escaped[1024];
        html_escape(entry->d_name, escaped, sizeof(escaped));

        char link[1024];
        if (is_dir) {
            snprintf(link, sizeof(link), "%s/", entry->d_name);
        } else {
            snprintf(link, sizeof(link), "%s", entry->d_name);
        }

        len += snprintf(out + len, maxlen - len,
            "<li><a href=\"%s%s\">%s%s</a></li>\n",
            uri, link, escaped, is_dir ? "/" : "");
    }

    closedir(dir);

    len += snprintf(out + len, maxlen - len,
        "</ul><hr></body></html>\n");

    return len;
}

int handle_static(const char *uri, char *body, int maxlen, const char **ctype) {
    // uri like /dir/path/to/file or /dir
    if (strncmp(uri, "/dir", 4) != 0) {
        return -1;
    }

    const char *path = uri + 4; // skip "/dir"
    if (*path == '/') path++;
    if (*path == '\0') path = ".";

    // check for ".." path traversal
    if (strstr(path, "..") != NULL) {
        return -1;
    }

    struct stat st;
    if (stat(path, &st) < 0) {
        return -1;
    }

    // directory
    if (S_ISDIR(st.st_mode)) {
        // check index.html
        char index_path[1024];
        snprintf(index_path, sizeof(index_path), "%s/index.html", path);

        if (access(index_path, R_OK) == 0) {
            path = index_path;
            goto serve_file;
        }

        // build directory listing
        *ctype = "text/html; charset=utf-8";
        return build_dir_listing_escape(path, uri, body, maxlen);
    }

serve_file:
    // regular file
    FILE *f = fopen(path, "rb");
    if (!f) {
        return -1;
    }

    *ctype = get_mime_type(path);
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize >= maxlen) {
        fclose(f);
        return -1;
    }

    int r = fread(body, 1, fsize, f);
    fclose(f);
    return r;
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

int handle_status(const char *path, int connfd) {
    int code = atoi(path + 8);
    char resp[256];
    int len;

    if (code == 301) {
        len = snprintf(resp, sizeof(resp),
            "HTTP/1.1 301 MOVED PERMANENTLY\r\n"
            "Location: /get\r\n"
            "Connection: close\r\n\r\n");
    } else if (code == 302) {
        len = snprintf(resp, sizeof(resp),
            "HTTP/1.1 302 FOUND\r\n"
            "Location: /raw\r\n"
            "Connection: close\r\n\r\n");
    } else {
        const char *status_text;
        switch (code) {
            case 200: status_text = "OK"; break;
            case 400: status_text = "Bad Request"; break;
            case 403: status_text = "Forbidden"; break;
            case 404: status_text = "Not Found"; break;
            case 500: status_text = "INTERNAL SERVER ERROR"; break;
            default: status_text = "Unknown"; break;
        }
        len = snprintf(resp, sizeof(resp),
            "HTTP/1.1 %d %s\r\nConnection: close\r\n\r\n",
            code, status_text);
    }
    write(connfd, resp, len);
    return 0;
}