#include <unity.h>
#include "components/composition/system_components.h"

void test_header_compiles(void) {
    TEST_ASSERT_TRUE(true);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_header_compiles);
    return UNITY_END();
}
