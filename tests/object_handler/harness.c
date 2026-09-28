#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "harness.h"

static const char * harness_case_name = "?";
static void (*harness_dump)(void);

void harness_set_dump(void (*dump)(void))
{
    harness_dump = dump;
}

static void harness_die(void)
{
    if (harness_dump != NULL)
        harness_dump();
    exit(1);
}

void harness_fail(const char * file, int line, const char * expr)
{
    fprintf(stderr, "%s:%d: %s: CHECK failed: %s\n", file, line, harness_case_name, expr);
    harness_die();
}

void harness_fail_eq(const char * file, int line, const char * a, const char * b,
                     long long va, long long vb)
{
    fprintf(stderr, "%s:%d: %s: CHECK_EQ failed: %s == %s (%lld != %lld)\n",
            file, line, harness_case_name, a, b, va, vb);
    harness_die();
}

void harness_skip(const char * why)
{
    printf("%s: skipped: %s\n", harness_case_name, why);
    exit(HARNESS_SKIP_CODE);
}

int harness_main(int argc, char ** argv, const harness_case_t * cases, size_t count)
{
    size_t i;

    if (argc == 2 && strcmp(argv[1], "--list") == 0) {
        for (i = 0; i < count; i++)
            puts(cases[i].name);
        return 0;
    }
    if (argc != 2) {
        fprintf(stderr, "usage: %s <case> | --list\n", argv[0]);
        return 2;
    }
    for (i = 0; i < count; i++) {
        if (strcmp(argv[1], cases[i].name) == 0) {
            harness_case_name = cases[i].name;
            cases[i].fn();
            printf("%s: ok\n", cases[i].name);
            return 0;
        }
    }
    fprintf(stderr, "unknown case: %s\n", argv[1]);
    return 2;
}
