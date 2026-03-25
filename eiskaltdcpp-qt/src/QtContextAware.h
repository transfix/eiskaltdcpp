/*
 * Copyright (C) 2026 Joe Rivera <transfix@sublevels.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include <cassert>

class QtContext;
namespace dcpp { class DCContext; }

/**
 * Mixin that gives any class access to its owning QtContext
 * and the dcpp::DCContext (core library context).
 *
 * Service classes (owned by QtContext) have their context set by
 * QtContext::createFoo() immediately after construction.  During
 * construction, before setQtContext() has been called, the member
 * qtCtx() falls back to the process-wide current() pointer that
 * QtContext registers in its own constructor.
 *
 * Non-service code can use the free function qtCtx() which returns
 * the current QtContext*.
 */
class QtContextAware {
public:
    /** Return per-instance context, falling back to the process-wide one. */
    [[nodiscard]] QtContext* qtCtx() const noexcept {
        return qtCtx_ ? qtCtx_ : current_;
    }
    void setQtContext(QtContext* ctx) noexcept { qtCtx_ = ctx; }

    /** Return the dcpp::DCContext for direct library access. */
    [[nodiscard]] dcpp::DCContext* dcCtx() const noexcept { return dcCtx_; }
    void setDcContext(dcpp::DCContext* ctx) noexcept { dcCtx_ = ctx; }
    static void setGlobalDcContext(dcpp::DCContext* ctx) noexcept { g_dcCtx_ = ctx; }
    [[nodiscard]] static dcpp::DCContext* globalDcContext() noexcept { return g_dcCtx_; }

    /** Process-wide current QtContext (set by QtContext ctor/dtor). */
    [[nodiscard]] static QtContext* current() noexcept { return current_; }
    static void setCurrent(QtContext* ctx) noexcept { current_ = ctx; }

protected:
    QtContextAware() noexcept : dcCtx_(g_dcCtx_) {}
    explicit QtContextAware(QtContext* ctx) noexcept : qtCtx_(ctx), dcCtx_(g_dcCtx_) {}
    ~QtContextAware() = default;

private:
    QtContext* qtCtx_ = nullptr;
    dcpp::DCContext* dcCtx_ = nullptr;
    static inline QtContext* current_ = nullptr;
    static inline dcpp::DCContext* g_dcCtx_ = nullptr;
};

/**
 * Free-function accessor — returns the process-wide QtContext.
 * Used by non-service code (widgets, models, free functions).
 */
[[nodiscard]] inline QtContext* qtCtx() noexcept {
    return QtContextAware::current();
}
