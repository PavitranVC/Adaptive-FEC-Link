/* Smoke test: the library links and reports a version string. */
#include <string.h>
#include "testlib.h"
#include "version.h"

TEST(version_string_is_not_empty) {
    CHECK(feclink_version() != NULL);
    CHECK(strlen(feclink_version()) > 0);
}

int main(void) {
    RUN(version_string_is_not_empty);
    return TEST_REPORT();
}
