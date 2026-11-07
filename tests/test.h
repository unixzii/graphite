#ifndef GRAPHITE_TEST_H
#define GRAPHITE_TEST_H

#include <graphite/types.h>

#include <stddef.h>

typedef struct
{
    const char *name;
    void (*run)(void);
} gr_test_case;

#ifdef __cplusplus
#define GR_TEST_CASE(function) (gr_test_case{#function, function})
#else
#define GR_TEST_CASE(function) ((gr_test_case){#function, function})
#endif
#define CHECK(expression)                                                                                              \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(expression))                                                                                             \
            gr_test_fail(__FILE__, __LINE__, #expression);                                                             \
    } while (0)
#define OK(expression) CHECK((expression) == GR_OK)
#define CHECK_NEAR(actual, expected) gr_test_near((actual), (expected), __FILE__, __LINE__, #actual " ~= " #expected)

#ifdef __cplusplus
#define GR_TEST_NORETURN [[noreturn]]
#else
#define GR_TEST_NORETURN _Noreturn
#endif

#ifdef __cplusplus
extern "C" {
#endif

GR_TEST_NORETURN void gr_test_fail(const char *file, int line, const char *expression);
void gr_test_near(double actual, double expected, const char *file, int line, const char *expression);
int gr_test_run(const char *suite, const gr_test_case *cases, size_t count, int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif
