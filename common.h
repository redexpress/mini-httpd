#ifndef COMMON_H
#define COMMON_H

#define PORT 8080
#define BUF_SIZE 16384
#define MAX_HEADERS 64
#define MAX_HEADER_LEN 1024

typedef struct {
    char buf[BUF_SIZE];
    int start;
    int end;
} stream_buf_t;

#endif