#!/usr/bin/env bash

set -e

SRC_DIR="./src"
BUILD_DIR="./build"
EXEC="app"
MODE="${1:-debug}"

COMMON_LIBS="-lm -lglfw -lvulkan"

mkdir -p "$BUILD_DIR"
mkdir -p "$BUILD_DIR/shaders"

slangc "$SRC_DIR/shaders/shader.slang" -target spirv -profile spirv_1_4 \
  -emit-spirv-directly -fvk-use-entrypoint-name \
  -entry vertMain -entry fragMain -o "$BUILD_DIR/shaders/slang.spv"

case "$MODE" in
  debug)
    CFLAGS="-g3 -O0 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -DDEBUG -DVK_ENABLE_VALIDATION "
    RUN_SANITIZED=0
    ;;
  debug-sanitize)
    CFLAGS="-g3 -O0 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -fanalyzer -fsanitize=address,undefined -fno-omit-frame-pointer -DDEBUG -DVK_ENABLE_VALIDATION"
    RUN_SANITIZED=1
    ;;
  release)
    CFLAGS="-O2 -DNDEBUG"
    RUN_SANITIZED=0
    ;;
  *)
    printf 'Usage: %s [debug|debug-sanitize|release]\n' "$0" >&2
    exit 1
    ;;
esac

gcc $CFLAGS "$SRC_DIR/main.c" -o "$BUILD_DIR/$EXEC" $COMMON_LIBS

if [ "$RUN_SANITIZED" -eq 1 ]; then
  ASAN_OPTIONS=detect_leaks=0 "$BUILD_DIR/$EXEC"
else
  "$BUILD_DIR/$EXEC"
fi
