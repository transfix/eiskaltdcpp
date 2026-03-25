/*
 * Copyright (C) 2026 Joe Rivera <transfix@sublevels.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

namespace dcpp { class DCContext; }

/**
 * Mixin that gives GTK classes access to the dcpp::DCContext.
 *
 * Mirrors the QtContextAware pattern.  Set once from main() via
 * setGlobalDcContext(), then every GTK class that inherits this mixin
 * can call dcCtx() to reach the core library context.
 */
class GtkContextAware {
public:
    [[nodiscard]] dcpp::DCContext* dcCtx() const noexcept { return dcCtx_ ? dcCtx_ : g_dcCtx_; }
    void setDcContext(dcpp::DCContext* ctx) noexcept { dcCtx_ = ctx; }
    static void setGlobalDcContext(dcpp::DCContext* ctx) noexcept { g_dcCtx_ = ctx; }
    [[nodiscard]] static dcpp::DCContext* globalDcContext() noexcept { return g_dcCtx_; }

protected:
    GtkContextAware() noexcept : dcCtx_(g_dcCtx_) {}
    ~GtkContextAware() = default;

private:
    dcpp::DCContext* dcCtx_ = nullptr;
    static inline dcpp::DCContext* g_dcCtx_ = nullptr;
};
