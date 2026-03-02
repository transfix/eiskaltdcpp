/*
 * TestContext — shared fixture for Phase 2+ tests that need SETTING() etc.
 *
 * Creates a minimal DCContext with ResourceManager, SettingsManager, and
 * LogManager.  No network, no hashing, no file I/O beyond a temp directory.
 * The global getContext() is wired so SETTING()/BOOLSETTING() macros work.
 *
 * Usage (Catch2):
 *   #include "TestContext.h"
 *   TEST_CASE("my test") {
 *       TestContext tc;   // creates temp dir, starts minimal context
 *       // ... SETTING(SLOTS) etc. now work with defaults ...
 *   }  // shutdown + cleanup on scope exit
 */

#pragma once

#include "stdinc.h"
#include "DCPlusPlus.h"
#include "DCContext.h"
#include "Util.h"

#include <filesystem>
#include <memory>
#include <string>

namespace dcpp {
namespace test {

struct TestContext {
    std::filesystem::path tmpDir;

    TestContext() {
        // Create a unique temp directory for this test run
        tmpDir = std::filesystem::temp_directory_path() / "eiskalt_test";
        // Append a unique suffix using the address of this object
        char suffix[32];
        snprintf(suffix, sizeof(suffix), "_%p", static_cast<void*>(this));
        tmpDir += suffix;
        std::filesystem::create_directories(tmpDir);

        // Use make_preferred() to ensure native separators (\ on Windows)
        tmpDir = tmpDir.make_preferred();
        auto dirStr = tmpDir.string() + std::string(1, PATH_SEPARATOR);

        // Initialize Util with paths redirected to our temp dir
        Util::PathsMap overrides;
        overrides[Util::PATH_USER_CONFIG] = dirStr;
        overrides[Util::PATH_USER_LOCAL]  = dirStr;
        overrides[Util::PATH_DOWNLOADS]   = dirStr + "Downloads" + std::string(1, PATH_SEPARATOR);
        Util::initialize(overrides);

        // Create minimal context (ResourceManager + SettingsManager + LogManager)
        auto ctx = std::make_unique<DCContext>();
        ctx->startupMinimal();
        dcpp::setContext(std::move(ctx));
    }

    ~TestContext() {
        if (dcpp::getContext()) {
            dcpp::getContext()->shutdown();
            dcpp::setContext(nullptr);
        }
        std::error_code ec;
        std::filesystem::remove_all(tmpDir, ec);
    }

    // Non-copyable
    TestContext(const TestContext&) = delete;
    TestContext& operator=(const TestContext&) = delete;
};

} // namespace test
} // namespace dcpp
