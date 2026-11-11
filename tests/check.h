/* check.h - a minimal test runner: TEST(name) registers, CHECK records a failure
   and goes on. `test_x NAME` runs one test; no argument runs them all. */
#ifndef CUI_REPL_CHECK_H
#define CUI_REPL_CHECK_H

#include <stdio.h>
#include <string.h>

typedef struct { const char *name; void (*fn)(void); } test_case;
static test_case tests_[64];
static int ntests_, failures_;

#define TEST(name)                                                              \
    static void test_##name(void);                                              \
    __attribute__((constructor)) static void register_##name(void) {            \
        tests_[ntests_++] = (test_case){#name, test_##name};                     \
    }                                                                           \
    static void test_##name(void)

#define CHECK(cond)                                                             \
    do {                                                                        \
        if (!(cond)) {                                                          \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
            failures_++;                                                        \
        }                                                                       \
    } while (0)

#define CHECK_CONTAINS(hay, needle)                                             \
    do {                                                                        \
        const char *h_ = (hay);                                                 \
        if (!h_ || !strstr(h_, (needle))) {                                     \
            fprintf(stderr, "%s:%d: expected \"%s\" in:\n---\n%s\n---\n", __FILE__, __LINE__, \
                    (needle), h_ ? h_ : "(null)");                              \
            failures_++;                                                        \
        }                                                                       \
    } while (0)

static int run_tests(int argc, char **argv) {
    int ran = 0;
    for (int i = 0; i < ntests_; i++) {
        if (argc > 1 && strcmp(argv[1], tests_[i].name)) continue;
        int before = failures_;
        tests_[i].fn();
        printf("%s %s\n", failures_ == before ? "ok  " : "FAIL", tests_[i].name);
        ran++;
    }
    if (!ran) fprintf(stderr, "no test named %s\n", argc > 1 ? argv[1] : "(any)");
    return failures_ || !ran;
}

#endif
