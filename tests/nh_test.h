/* Night Heist - minimal unit test harness. */
#ifndef NH_TEST_H
#define NH_TEST_H

#include <math.h>
#include <stdio.h>

extern int nh_test_failures;
extern int nh_test_checks;

#define CHECK(cond)                                                                    \
    do {                                                                               \
        nh_test_checks++;                                                              \
        if (!(cond)) {                                                                 \
            nh_test_failures++;                                                        \
            fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
        }                                                                              \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                          \
    do {                                                                               \
        double a_ = (double)(a), b_ = (double)(b);                                     \
        nh_test_checks++;                                                              \
        if (fabs(a_ - b_) > (eps)) {                                                   \
            nh_test_failures++;                                                        \
            fprintf(stderr, "  FAIL %s:%d: %s = %g, expected %g\n", __FILE__, __LINE__, \
                    #a, a_, b_);                                                       \
        }                                                                              \
    } while (0)

typedef void (*NhTestFn)(void);

#endif
