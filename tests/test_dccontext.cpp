/*
 * Copyright (C) 2026 Joe Rivera <transfix@sublevels.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

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

// ── Singleton::newInstance / deleteInstance tests ────────────────────────

// FakeMgr must live in namespace dcpp because MSVC requires explicit
// specialisations to reside in the same namespace as the primary template.
namespace dcpp {
struct FakeMgr : public Singleton<FakeMgr> {
    int value = 42;
};
template<> FakeMgr* Singleton<FakeMgr>::instance = nullptr;
} // namespace dcpp
using dcpp::FakeMgr;

TEST_CASE("Singleton newInstance creates and deleteInstance destroys", "[Singleton]") {
    REQUIRE(FakeMgr::getInstance() == nullptr);
    FakeMgr::newInstance();
    REQUIRE(FakeMgr::getInstance() != nullptr);
    REQUIRE(FakeMgr::getInstance()->value == 42);
    FakeMgr::deleteInstance();
    REQUIRE(FakeMgr::getInstance() == nullptr);
}

TEST_CASE("Singleton deleteInstance is safe when already null", "[Singleton]") {
    REQUIRE(FakeMgr::getInstance() == nullptr);
    FakeMgr::deleteInstance(); // Should not crash
    SUCCEED();
}
