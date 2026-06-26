/*
 * Copyright (C) 2026 Joe Rivera <transfix@sublevels.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/*
 * Unit tests for dcpp::DCContext and dcpp::ContextAware.
 */

#include <catch2/catch_test_macros.hpp>

#include "stdinc.h"
#include "DCContext.h"

using namespace dcpp;

// ── ContextAware tests ─────────────────────────────────────────────────

TEST_CASE("ContextAware ctx returns the injected DCContext", "[ContextAware]") {
    struct TestAware : public ContextAware {
        explicit TestAware(DCContext& ctx) : ContextAware(ctx) {}
    };

    DCContext ctx;
    TestAware obj(ctx);
    REQUIRE(&obj.ctx() == &ctx);
}

TEST_CASE("ContextAware ctx is the same reference for multiple objects", "[ContextAware]") {
    struct TestAware : public ContextAware {
        explicit TestAware(DCContext& ctx) : ContextAware(ctx) {}
    };

    DCContext ctx;
    TestAware a(ctx), b(ctx);

    REQUIRE(&a.ctx() == &b.ctx());
}

// ── DCContext lifecycle tests ───────────────────────────────────────────

TEST_CASE("DCContext can be constructed and destroyed", "[DCContext]") {
    DCContext ctx;
    REQUIRE_FALSE(ctx.isRunning());
    // All accessor pointers should be nullptr before startup
    REQUIRE(ctx.getSettingsManager() == nullptr);
    REQUIRE(ctx.getTimerManager() == nullptr);
    REQUIRE(ctx.getClientManager() == nullptr);
    REQUIRE(ctx.getDynDNS() == nullptr);
#ifdef LUA_SCRIPT
    REQUIRE(ctx.getScriptManager() == nullptr);
#endif
}

TEST_CASE("Multiple DCContext instances can coexist", "[DCContext]") {
    DCContext ctx1;
    DCContext ctx2;
    REQUIRE_FALSE(ctx1.isRunning());
    REQUIRE_FALSE(ctx2.isRunning());
}
