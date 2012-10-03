#include <cstdio>
#include <cstdlib>

static inline void test_no_op() {}
void test_fail(const char *cond, const char *file, int line);

#define TEST(cond) (!(cond) ? test_fail(#cond, __FILE__, __LINE__) : test_no_op() )
