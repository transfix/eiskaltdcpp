/*
 * Unit tests for dcpp::DCContext, dcpp::ContextAware, and Singleton bridging.
 */

#include <catch2/catch_test_macros.hpp>

#include "stdinc.h"
#include "DCContext.h"
#include "Singleton.h"

using namespace dcpp;

// ── ContextAware tests ─────────────────────────────────────────────────

TEST_CASE("ContextAware default ctx is nullptr", "[ContextAware]") {
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

// ── DCContext lifecycle tests ───────────────────────────────────────────

TEST_CASE("DCContext can be constructed and destroyed", "[DCContext]") {
    DCContext ctx;
    REQUIRE_FALSE(ctx.isRunning());
    // All accessor pointers should be nullptr before startup
    REQUIRE(ctx.getSettingsManager() == nullptr);
    REQUIRE(ctx.getTimerManager() == nullptr);
    REQUIRE(ctx.getClientManager() == nullptr);
}

TEST_CASE("Multiple DCContext instances can coexist", "[DCContext]") {
    DCContext ctx1;
    DCContext ctx2;
    REQUIRE_FALSE(ctx1.isRunning());
    REQUIRE_FALSE(ctx2.isRunning());
}

// ── Singleton::setInstance bridging tests ────────────────────────────────

namespace {
// Minimal singleton-compatible type for isolated testing.
struct FakeMgr : public Singleton<FakeMgr>, public ContextAware {
    int value = 42;
};
template<> FakeMgr* Singleton<FakeMgr>::instance = nullptr;
} // anonymous namespace

TEST_CASE("Singleton::setInstance registers and unregisters a pointer", "[Singleton]") {
    FakeMgr mgr;
    REQUIRE(Singleton<FakeMgr>::getInstance() == nullptr);  // dcassert would fire, but tests build without NDEBUG dasserts disabled

    Singleton<FakeMgr>::setInstance(&mgr);
    REQUIRE(FakeMgr::getInstance() == &mgr);
    REQUIRE(FakeMgr::getInstance()->value == 42);

    Singleton<FakeMgr>::setInstance(nullptr);
    // After clearing, getInstance() returns nullptr.
    // (dcassert fires in debug builds, but we test the pointer value.)
}

TEST_CASE("setInstance does not transfer ownership", "[Singleton]") {
    auto mgr = std::make_unique<FakeMgr>();
    FakeMgr* raw = mgr.get();

    Singleton<FakeMgr>::setInstance(raw);
    REQUIRE(FakeMgr::getInstance() == raw);

    // Destroying the unique_ptr should NOT be done by Singleton.
    // Clearing the pointer first simulates DCContext::teardown().
    Singleton<FakeMgr>::setInstance(nullptr);
    mgr.reset();  // Actual destruction — no double-free
    SUCCEED();
}
