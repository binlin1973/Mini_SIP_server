#ifndef TEST_COMMON_H
#define TEST_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *name;
    int (*fn)(void);
} test_case_t;

typedef struct {
    int total;
    int passed;
    int failed;
    int assertions_total;
    int assertions_failed;
} test_stats_t;

static inline void test_stats_init(test_stats_t *stats) {
    if (stats != NULL) {
        memset(stats, 0, sizeof(*stats));
    }
}

static test_stats_t *g_current_stats = NULL;

static inline void test_set_active_stats(test_stats_t *stats) {
    g_current_stats = stats;
}

static inline test_stats_t *test_get_stats(test_stats_t *stats) {
    return stats != NULL ? stats : g_current_stats;
}

static inline int test_assert_true(test_stats_t *stats, int condition, const char *expr,
                                   const char *file, int line) {
    test_stats_t *active = test_get_stats(stats);
    if (active != NULL) {
        active->assertions_total++;
        if (!condition) {
            active->assertions_failed++;
            fprintf(stderr, "Assertion failed: (%s) at %s:%d\n", expr, file, line);
            return 1;
        }
        return 0;
    }
    if (!condition) {
        fprintf(stderr, "Assertion failed: (%s) at %s:%d\n", expr, file, line);
        return 1;
    }
    return 0;
}

static inline int test_assert_int_eq(test_stats_t *stats, long long actual, long long expected,
                                     const char *expr_actual, const char *expr_expected,
                                     const char *file, int line) {
    test_stats_t *active = test_get_stats(stats);
    if (active != NULL) {
        active->assertions_total++;
    }
    if (actual != expected) {
        if (active != NULL) {
            active->assertions_failed++;
        }
        fprintf(stderr,
                "Assertion failed: (%s == %s) at %s:%d\n  Actual:   %lld\n  Expected: %lld\n",
                expr_actual, expr_expected, file, line, actual, expected);
        return 1;
    }
    return 0;
}

static inline int test_assert_str_contains(test_stats_t *stats, const char *haystack,
                                           const char *needle, const char *expr_haystack,
                                           const char *expr_needle, const char *file, int line) {
    test_stats_t *active = test_get_stats(stats);
    if (active != NULL) {
        active->assertions_total++;
    }
    int failure = (haystack == NULL || needle == NULL || strstr(haystack, needle) == NULL);
    if (failure) {
        if (active != NULL) {
            active->assertions_failed++;
        }
        fprintf(stderr,
                "Assertion failed: (%s contains %s) at %s:%d\n  Haystack: %s\n  Needle:   %s\n",
                expr_haystack, expr_needle, file, line,
                haystack ? haystack : "(null)", needle ? needle : "(null)");
    }
    return failure;
}

static inline int run_tests(const test_case_t *cases, size_t count, test_stats_t *stats) {
    if (cases == NULL || stats == NULL) {
        return EXIT_FAILURE;
    }

    test_stats_init(stats);
    test_set_active_stats(stats);

    printf("Running %zu test(s)...\n", count);
    for (size_t i = 0; i < count; ++i) {
        printf("  [%zu/%zu] %s... ", i + 1, count, cases[i].name);
        fflush(stdout);
        int failures = cases[i].fn();
        stats->total++;
        if (failures == 0) {
            printf("PASSED\n");
            stats->passed++;
        } else {
            printf("FAILED (%d assertion%s)\n", failures, failures == 1 ? "" : "s");
            stats->failed++;
        }
    }

    printf("\nTest summary: %d total, %d passed, %d failed\n",
           stats->total, stats->passed, stats->failed);
    printf("Assertions: %d total, %d failed\n",
           stats->assertions_total, stats->assertions_failed);

    test_set_active_stats(NULL);

    return stats->failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

#define EXPECT_TRUE(expr)                                                         \
    do {                                                                         \
        failures += test_assert_true(NULL, (expr), #expr, __FILE__, __LINE__);    \
    } while (0)

#define EXPECT_EQ_INT(actual, expected)                                           \
    do {                                                                         \
        failures += test_assert_int_eq(NULL, (long long)(actual),                \
                                       (long long)(expected), #actual, #expected,\
                                       __FILE__, __LINE__);                      \
    } while (0)

#define EXPECT_STRCONTAINS(haystack, needle)                                      \
    do {                                                                         \
        failures += test_assert_str_contains(NULL, (haystack), (needle),         \
                                             #haystack, #needle,                \
                                             __FILE__, __LINE__);                \
    } while (0)

#endif /* TEST_COMMON_H */
