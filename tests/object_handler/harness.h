#ifndef HARNESS_H
#define HARNESS_H

#include <stddef.h>

typedef struct {
    const char * name;
    void (*fn)(void);
} harness_case_t;

/* CMake registers one ctest per TEST_CASE line, so each case runs in its own process. */
#define TEST_CASE(name)         static void test_##name(void)
#define HARNESS_CASE(name)      { #name, test_##name }
#define HARNESS_SKIP_CODE       77

#define CHECK(expr) \
    do { if (!(expr)) harness_fail(__FILE__, __LINE__, #expr); } while (0)

#define CHECK_EQ(a, b) \
    do { \
        long long check_a_ = (long long)(a), check_b_ = (long long)(b); \
        if (check_a_ != check_b_) \
            harness_fail_eq(__FILE__, __LINE__, #a, #b, check_a_, check_b_); \
    } while (0)

void harness_fail(const char * file, int line, const char * expr);
void harness_fail_eq(const char * file, int line, const char * a, const char * b,
                     long long va, long long vb);
void harness_skip(const char * why);
void harness_set_dump(void (*dump)(void));
int  harness_main(int argc, char ** argv, const harness_case_t * cases, size_t count);

#define HARNESS_MAIN(cases) \
    int main(int argc, char ** argv) \
    { return harness_main(argc, argv, (cases), sizeof(cases) / sizeof((cases)[0])); }

#endif /* HARNESS_H */
