#ifndef GRAPHITE_TEST_H
#define GRAPHITE_TEST_H
#include <graphite/types.h>
#include <stddef.h>

typedef struct
{
    const char *name;
    void (*run)(void);
} gr_test_case;

#define GR_TEST_CASE(function)                                                                                         \
    {                                                                                                                  \
        #function, function                                                                                            \
    }
#define CHECK(expression)                                                                                              \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(expression))                                                                                             \
            gr_test_fail(__FILE__, __LINE__, #expression);                                                             \
    } while (0)
#define OK(expression) CHECK((expression) == GR_OK)
#define NEAR(actual, expected) gr_test_near((actual), (expected), __FILE__, __LINE__, #actual " ~= " #expected)

_Noreturn void gr_test_fail(const char *file, int line, const char *expression);
void gr_test_near(double actual, double expected, const char *file, int line, const char *expression);
int gr_test_run(const char *suite, const gr_test_case *cases, size_t count, int argc, char **argv);
#endif
