#include "testutil.h"

void test_fail(const char *cond, const char *file, int line)
{
    fprintf(stderr, "FAILED: \"%s\" at %s:%d\n", cond, file, line);
    exit( 1 );
}
