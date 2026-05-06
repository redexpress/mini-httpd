#!/bin/sh

method=""
path=""
proto=""
body=""

echo "{"
echo "  \"args\": {},"
echo "  \"headers\": {"

first=1

while IFS= read -r line; do

    case "$line" in
        __method:*)
            method="${line#__method: }"
            ;;
        __path:*)
            path="${line#__path: }"
            ;;
        __proto:*)
            proto="${line#__proto: }"
            ;;
        __body:*)
            body="${line#__body: }"
            break
            ;;
        *:*)
            key="${line%%:*}"
            val="${line#*:}"
            val="${val# }"

            if [ $first -eq 0 ]; then
                echo ","
            fi
            first=0

            printf "    \"%s\": \"%s\"" "$key" "$val"
            ;;
    esac

done

echo ""
echo "  },"

echo "  \"origin\": \"script\","
echo "  \"url\": \"$path\""

echo "}"