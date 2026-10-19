#include <stdbool.h>
#include <stdio.h>

#include "nh_test.h"

int nh_test_failures;
int nh_test_checks;

#define TESTS(X)               \
    X(test_math)               \
    X(test_rng)                \
    X(test_arena)              \
    X(test_fixedstep)          \
    X(test_light)              \
    X(test_texconv)            \
    X(test_meshgen)            \
    X(test_font)               \
    X(test_demo_sim)

#define X(name) void name(void);
TESTS(X)
#undef X

int main(void)
{
    static const struct {
        const char *name;
        NhTestFn fn;
    } tests[] = {
#define X(name) { #name, name },
        TESTS(X)
#undef X
    };
    int failed_tests = 0;
    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        int before = nh_test_failures;
        tests[i].fn();
        bool ok = nh_test_failures == before;
        printf("%-20s %s\n", tests[i].name, ok ? "ok" : "FAILED");
        if (!ok)
            failed_tests++;
    }
    printf("%d checks, %d failures in %d test(s)\n", nh_test_checks, nh_test_failures, failed_tests);
    return nh_test_failures ? 1 : 0;
}
