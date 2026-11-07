#include "test.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The runner changes these only between cases, after each case joins workers. */
static const char *current_suite = "unknown";
static const char *current_case = "unknown";

_Noreturn void gr_test_fail(const char *file, int line, const char *expression)
{
    fprintf(stderr, "[  FAILED  ] %s.%s\n%s:%d: %s\n", current_suite, current_case, file, line, expression);
    fflush(stderr);
    abort();
}

void gr_test_near(double actual, double expected, const char *file, int line, const char *expression)
{
    if (!isfinite(actual) || !isfinite(expected) || fabs(actual - expected) > 1e-9)
    {
        fprintf(stderr, "actual=%.17g expected=%.17g tolerance=1e-9\n", actual, expected);
        gr_test_fail(file, line, expression);
    }
}

int gr_test_run(const char *suite, const gr_test_case *cases, size_t count, int argc, char **argv)
{
    if (argc > 2)
    {
        fprintf(stderr, "Usage: %s [case-name | --list]\n", argv[0]);
        return EXIT_FAILURE;
    }
    current_suite = suite;
    size_t executed = 0;
    for (size_t i = 0; i < count; ++i)
    {
        if (argc == 2 && strcmp(argv[1], "--list") == 0)
        {
            puts(cases[i].name);
            continue;
        }
        if (argc == 2 && strcmp(argv[1], cases[i].name) != 0)
            continue;
        current_case = cases[i].name;
        printf("[ RUN      ] %s.%s\n", suite, current_case);
        fflush(stdout);
        cases[i].run();
        printf("[       OK ] %s.%s\n", suite, current_case);
        fflush(stdout);
        ++executed;
    }
    if (argc == 2 && strcmp(argv[1], "--list") == 0)
        return EXIT_SUCCESS;
    if (executed == 0)
    {
        fprintf(stderr, "Unknown test case: %s\n", argc == 2 ? argv[1] : "(none)");
        return EXIT_FAILURE;
    }
    printf("[  PASSED  ] %zu case(s) in %s\n", executed, suite);
    return EXIT_SUCCESS;
}
