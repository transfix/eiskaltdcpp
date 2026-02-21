/*
 * Unit tests for dcpp::DCContext and dcpp::ContextAware
 */

#include <catch2/catch_test_macros.hpp>

#include "stdinc.h"
#include "DCContext.h"

using namespace dcpp;

TEST_CASE("DCContext can be constructed and destroyed", "[DCContext]") {
    DCContext ctx;
    // Smoke test: construction and destruction should not crash
    SUCCEED();
}

TEST_CASE("Multiple DCContext instances can coexist", "[DCContext]") {
    DCContext ctx1;
    DCContext ctx2;
    // Both should be independent
    SUCCEED();
}

TEST_CASE("ContextAware default ctx is nullptr", "[ContextAware]") {
    // Create a concrete ContextAware-derived class for testing
    struct TestAware : public ContextAware {};

    TestAware obj;
    REQUIRE(obj.ctx() == nullptr);
}

TEST_CASE("ContextAware::setContext stores and retrieves pointer", "[ContextAware]") {
    struct TestAware : public ContextAware {};

    DCContext ctx;
    TestAware obj;

    obj.setContext(&ctx);
    REQUIRE(obj.ctx() == &ctx);

    obj.setContext(nullptr);
    REQUIRE(obj.ctx() == nullptr);
}
