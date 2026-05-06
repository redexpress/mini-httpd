# mini-httpd

[English](./README.md) | [中文](./README.zh.md)

一个轻量级的 HTTP 服务器，支持 shell 脚本执行。

## 快速开始

```bash
cmake . && make
./minihttpd 8080
```

> 项目文件 `busybox` 下载自 https://busybox.net/downloads/binaries/1.26.2-defconfig-multiarch/busybox-x86_64

## 端点

| 端点 | 描述 |
|------|------|
| `/` | 返回空 JSON 对象 `{}` |
| `/get?foo=bar` | GET 请求检查器，返回 method, headers, args, origin, url |
| `/post` | POST 请求检查器，返回 method, headers, args, data, json, form, origin, url |
| `/raw` | 返回原始 HTTP 请求数据 |
| `/exec/<script>` | 执行 shell 脚本，将请求作为 stdin 传入 |

## Shell 执行 (`/exec`)

将 shell 脚本放在当前目录，通过 HTTP 调用：

```
./your_script.sh  →  http://localhost:8080/exec/your_script
```

**传递给脚本 stdin 的请求格式：**
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

脚本的 stdout 作为 HTTP 响应体返回。

参考 `sample.sh` 了解解析请求格式的示例实现。

**示例：**

创建 `hello.sh`：
```sh
#!/bin/sh
echo "Hello from shell!"
echo "Method: $__method"
echo "Path: $__path"
cat  # echo stdin
```
```bash
chmod +x hello.sh
curl http://localhost:8080/exec/hello
```

## 构建要求

- POSIX 系统（Linux、macOS、WSL）
- GCC 或 Clang
- CMake 3.14+
