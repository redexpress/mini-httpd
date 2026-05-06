# mini-httpd

[English](./README.md) | [中文](./README.zh.md)

A lightweight HTTP server with shell script execution support.

## Quick Start

```bash
cmake . && make
./minihttpd 8080
```

> project file `busybox` download from https://busybox.net/downloads/binaries/1.26.2-defconfig-multiarch/busybox-x86_64

## Endpoints

| Endpoint | Description |
|----------|-------------|
| `/` | Returns empty JSON object `{}` |
| `/get?foo=bar` | GET request inspector, returns method, headers, args, origin, url |
| `/post` | POST request inspector, returns method, headers, args, data, json, form, origin, url |
| `/raw` | Returns raw HTTP request data |
| `/exec/<script>` | Execute shell script, passes request as stdin |

## Shell Execution (`/exec`)

Drop shell scripts in the current directory and call them via HTTP:

```
./your_script.sh  →  http://localhost:8080/exec/your_script
```

**Request format passed to script stdin:**
```
__method: GET
__path: /exec/test
__proto: HTTP/1.1
Host: localhost:8080
User-Agent: curl/7.88.1
Accept: */*
Content-Length: 11

hello world
```

The script's stdout is returned as the HTTP response body.

See `sample.sh` for a reference implementation that parses the request format.

**Example:**

Create `hello.sh`:
```sh
#!/bin/sh
echo "Hello from shell!"
echo "Method: $__method"
echo "Path: $__path"
cat  # echo stdin
```
```bash
curl http://localhost:8080/exec/hello
```

## Build Requirements

- POSIX system (Linux, macOS, WSL)
- GCC or Clang
- CMake 3.14+