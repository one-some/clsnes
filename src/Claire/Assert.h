#pragma once

#include <stdlib.h>
#include <stdio.h>

#define ASSERT(condition, format, ...) \
    _ASSERT(condition, __FILE__, __func__, __LINE__, format, ##__VA_ARGS__)

#define ASSERT_NOT_REACHED(format, ...) \
    _ASSERT_NOT_REACHED(__FILE__, __func__, __LINE__, format, ##__VA_ARGS__)

#define ASSERT_BUF_SIZE 2048

void _ASSERT(
    bool condition,
    const char* file,
    const char* func,
    int line,
    const char* format,
    ...
) {
    if (condition) return;

    // Or so
    char message[ASSERT_BUF_SIZE];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    printf("[%s -> %s:%d] Assertion failed! :: %s\n", file, func, line, message);
    exit(1);
}

[[noreturn]] void _ASSERT_NOT_REACHED(
    const char* file,
    const char* func,
    int line,
    const char* format,
    ...
) {
    char message[ASSERT_BUF_SIZE];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    printf("[%s -> %s:%d] ASSERT_NOT_REACHED reached! :: %s\n", file, func, line, message);
    exit(1);
}
