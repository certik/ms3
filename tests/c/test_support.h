#pragma once

/*
 * Shared helpers for the C test suites. Each suite lives in its own
 * tests/c/test_<suite>.c and defines `void test_<suite>(void)`; main.c runs
 * the suites compiled into the binary (scripts/build.mjs --suite ...).
 *
 * Checks never compile out, print the failing expression with file/line
 * (and both values where available) and exit with status 1. Only corec I/O
 * is used, so the same binary runs natively, under Wasmtime and under Node.
 * Note: corec's base/format.h (pulled in by base/io.h) defines macros named
 * A, format, COUNT_ARGS and APPLY_*; avoid those identifiers in tests.
 */

#include <base/io.h>
#include <base/math.h>
#include <base/mem.h>
#include <base/numconv.h>
#include <platform/platform.h>

#include "engine.h"
#include "runtime.h"

static inline void test_print(const char *text) {
    ciovec_t iov;
    iov.buf = text;
    iov.buf_len = base_strlen(text);
    /* corec's write_all only advances past an iovec once bytes are written,
     * so a zero-length one would loop forever: write nothing instead. */
    if (iov.buf_len == 0) return;
    write_all(PLATFORM_STDOUT_FD, &iov, 1);
}

static inline void test_print_u64(uint64_t value) {
    char buf[32];
    size_t len = uint64_to_str(value, buf);
    buf[len] = '\0';
    test_print(buf);
}

static inline void test_print_hex(uint64_t value) {
    char buf[32];
    size_t len = uint64_to_hex_str(value, buf, 0);
    buf[len] = '\0';
    test_print("0x");
    test_print(buf);
}

/* Prints "  - name" so a failure can be located in the log. */
static inline void test_case(const char *name) {
    test_print("  - ");
    test_print(name);
    test_print("\n");
}

static inline void test_fail_begin(const char *what, const char *file, unsigned int line,
                                   const char *func) {
    char line_text[32];
    size_t len = uint64_to_str(line, line_text);
    line_text[len] = '\0';
    test_print("FAIL ");
    test_print(file);
    test_print(":");
    test_print(line_text);
    test_print(" in ");
    test_print(func);
    test_print("(): ");
    test_print(what);
}

static inline void test_fail_end(void) {
    test_print("\n");
    platform_exit(1);
}

static inline void test_check(bool ok, const char *what, const char *file, unsigned int line,
                              const char *func) {
    if (ok) return;
    test_fail_begin(what, file, line, func);
    test_fail_end();
}

static inline void test_check_u64(uint64_t actual, uint64_t expected, const char *what,
                                  const char *file, unsigned int line, const char *func) {
    if (actual == expected) return;
    test_fail_begin(what, file, line, func);
    test_print(" (actual ");
    test_print_u64(actual);
    test_print(" = ");
    test_print_hex(actual);
    test_print(", expected ");
    test_print_u64(expected);
    test_print(" = ");
    test_print_hex(expected);
    test_print(")");
    test_fail_end();
}

static inline void test_check_status(ms_status actual, ms_status expected, const char *what,
                                     const char *file, unsigned int line, const char *func) {
    if (actual == expected) return;
    test_fail_begin(what, file, line, func);
    test_print(" (actual ");
    test_print(ms_status_name(actual));
    test_print(", expected ");
    test_print(ms_status_name(expected));
    test_print(")");
    test_fail_end();
}

/* corec's double_to_str_e loops forever on infinities: guard specials. */
static inline void test_print_double(double value) {
    char buf[64];
    if (value != value) {
        test_print("nan");
    } else if (value - value != 0.0) {
        test_print(value > 0 ? "inf" : "-inf");
    } else {
        buf[double_to_str_e(value, buf, 17)] = '\0';
        test_print(buf);
    }
}

static inline void test_check_near(double actual, double expected, double tolerance,
                                   const char *what, const char *file, unsigned int line,
                                   const char *func) {
    double diff = actual > expected ? actual - expected : expected - actual;
    if (diff <= tolerance) return;
    test_fail_begin(what, file, line, func);
    test_print(" (actual ");
    test_print_double(actual);
    test_print(", expected ");
    test_print_double(expected);
    test_print(")");
    test_fail_end();
}

#define CHECK(cond) test_check((cond), "CHECK(" #cond ")", __FILE__, __LINE__, __func__)
#define CHECK_EQ(actual, expected)                                                         \
    test_check_u64((uint64_t)(actual), (uint64_t)(expected), #actual " == " #expected,   \
                   __FILE__, __LINE__, __func__)
#define CHECK_STATUS(expr, expected)                                                       \
    test_check_status((expr), (expected), #expr, __FILE__, __LINE__, __func__)
#define CHECK_NEAR(actual, expected, tolerance)                                            \
    test_check_near((actual), (expected), (tolerance), #actual " ~ " #expected, __FILE__, \
                    __LINE__, __func__)

/* Deterministic injected clock: every reading returns `now`, then advances
 * it by `step`. Tests may also set `now` directly between readings. */
typedef struct fake_clock {
    double now;
    double step;
    uint32_t reads;
} fake_clock;

static inline double fake_clock_read(void *ctx) {
    fake_clock *fake = (fake_clock *)ctx;
    double value = fake->now;
    fake->now += fake->step;
    fake->reads++;
    return value;
}

static inline void fake_clock_init(rt_clock *clock, fake_clock *fake, double start, double step) {
    fake->now = start;
    fake->step = step;
    fake->reads = 0;
    rt_clock_init(clock, fake_clock_read, fake);
}
