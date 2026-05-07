#define CATCH_CONFIG_MAIN
#include <catch_amalgamated.hpp>
#include <cstring>
#include <vector>
#include <tuple>

#define _True 1
#define _False 0

// Test helpers - copied from main.c for unit testing
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

int path_match(const char *path, const char *route) {
    int n = strlen(route);
    return strncmp(path, route, n) == 0 &&
           (path[n] == '\0' || path[n] == '?');
}

int build_raw_text(char *req, int len, char *out, int maxlen) {
    if (len > maxlen - 1) len = maxlen - 1;
    memcpy(out, req, len);
    out[len] = '\0';
    return len;
}

TEST_CASE("build_args_from_path", "[helpers]") {
    char buf[512];

    SECTION("empty query") {
        build_args_from_path("/get", buf, sizeof(buf));
        REQUIRE(strcmp(buf, "{}") == 0);
    }

    SECTION("single param") {
        build_args_from_path("/get?foo=bar", buf, sizeof(buf));
        REQUIRE(strcmp(buf, "{\"foo\": \"bar\"}") == 0);
    }

    SECTION("multiple params") {
        build_args_from_path("/get?foo=bar&name=john", buf, sizeof(buf));
        REQUIRE(strcmp(buf, "{\"foo\": \"bar\", \"name\": \"john\"}") == 0);
    }

    SECTION("empty value") {
        build_args_from_path("/get?foo=", buf, sizeof(buf));
        REQUIRE(strcmp(buf, "{\"foo\": \"\"}") == 0);
    }

    SECTION("no value") {
        build_args_from_path("/get?foo", buf, sizeof(buf));
        REQUIRE(strcmp(buf, "{\"foo\": \"\"}") == 0);
    }
}

TEST_CASE("path_match", "[helpers]") {
    SECTION("exact match") {
        REQUIRE(path_match("/get", "/get") != _False);
    }

    SECTION("with query string") {
        REQUIRE(path_match("/get?foo=bar", "/get") != _False);
    }

    SECTION("no match") {
        REQUIRE(path_match("/post", "/get") == _False);
    }

    SECTION("prefix no match") {
        REQUIRE(path_match("/gets", "/get") == _False);
    }
}

TEST_CASE("build_raw_text", "[helpers]") {
    char buf[1024];
    const char *input = "GET / HTTP/1.1\r\n\r\n";

    SECTION("normal copy") {
        int len = build_raw_text((char*)input, strlen(input), buf, sizeof(buf));
        REQUIRE(len == strlen(input));
        REQUIRE(strcmp(buf, input) == 0);
    }

    SECTION("truncate to maxlen") {
        char small[16];
        int len = build_raw_text((char*)input, strlen(input), small, sizeof(small));
        REQUIRE(len == sizeof(small) - 1);
    }
}
