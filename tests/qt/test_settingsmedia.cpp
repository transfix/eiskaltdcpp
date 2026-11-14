/*
 * Tests for SettingsMedia — the camera/microphone preferences page used by
 * NMDCpb calls. The page loads device + default-state preferences in its
 * constructor and writes them back in ok(). These tests verify that the
 * persisted WulforSettings keys round-trip through load → widget → ok().
 *
 * On a build without Qt Multimedia the device combo boxes only carry the
 * "System default" entry, but the default-mute checkboxes and the settings
 * plumbing still work, which is what we assert here.
 *
 * Pure Qt (no live hub); runs headless with QT_QPA_PLATFORM=offscreen.
 */

#include <catch2/catch_test_macros.hpp>

#include <QCheckBox>
#include <QComboBox>

#include "SettingsMedia.h"
#include "WulforSettings.h"
#include "QtContext.h"
#include "QtContextAware.h"
#include "../TestContext.h"

#include <memory>

namespace {

std::unique_ptr<dcpp::test::TestContext> g_tc;
std::unique_ptr<QtContext> g_qtCtx;

void cleanupQtContext() { g_qtCtx.reset(); }

void ensureSettings() {
    if (!g_tc)
        g_tc = std::make_unique<dcpp::test::TestContext>();
    if (!qtCtx()) {
        g_qtCtx = std::make_unique<QtContext>(*dcpp::getContext());
        g_qtCtx->createSettings();
        std::atexit(cleanupQtContext);
    }
}

QCheckBox *checkboxWithText(SettingsMedia &m, const QString &needle) {
    for (QCheckBox *c : m.findChildren<QCheckBox *>())
        if (c->text().contains(needle))
            return c;
    return nullptr;
}

} // namespace

// ─── Construction ───────────────────────────────────────────────────────

TEST_CASE("SettingsMedia: constructs and lists the default device entry",
          "[qt][settingsmedia]") {
    ensureSettings();
    SettingsMedia m;

    auto combos = m.findChildren<QComboBox *>();
    REQUIRE(combos.size() >= 2);
    // Every combo carries at least the "System default" entry (empty id).
    for (QComboBox *c : combos) {
        REQUIRE(c->count() >= 1);
        REQUIRE(c->itemData(0).toString().isEmpty());
    }
}

// ─── ok() persists the default-mute checkboxes ──────────────────────────

TEST_CASE("SettingsMedia: ok() writes the default mute/camera state",
          "[qt][settingsmedia]") {
    ensureSettings();
    auto *ws = qtCtx()->settings();
    ws->setBool(WB_MEDIA_MIC_MUTED_DEFAULT, false);
    ws->setBool(WB_MEDIA_CAMERA_OFF_DEFAULT, false);

    SettingsMedia m;
    QCheckBox *mic = checkboxWithText(m, "microphone muted");
    QCheckBox *cam = checkboxWithText(m, "camera turned off");
    REQUIRE(mic != nullptr);
    REQUIRE(cam != nullptr);

    mic->setChecked(true);
    cam->setChecked(true);
    m.ok();

    REQUIRE(ws->getBool(WB_MEDIA_MIC_MUTED_DEFAULT) == true);
    REQUIRE(ws->getBool(WB_MEDIA_CAMERA_OFF_DEFAULT) == true);
}

// ─── loadSettings restores the saved checkbox state ─────────────────────

TEST_CASE("SettingsMedia: reflects previously-saved default state on load",
          "[qt][settingsmedia]") {
    ensureSettings();
    auto *ws = qtCtx()->settings();
    ws->setBool(WB_MEDIA_MIC_MUTED_DEFAULT, true);
    ws->setBool(WB_MEDIA_CAMERA_OFF_DEFAULT, false);

    SettingsMedia m;
    QCheckBox *mic = checkboxWithText(m, "microphone muted");
    QCheckBox *cam = checkboxWithText(m, "camera turned off");
    REQUIRE(mic != nullptr);
    REQUIRE(cam != nullptr);
    REQUIRE(mic->isChecked() == true);
    REQUIRE(cam->isChecked() == false);
}

// ─── device selection persists an empty id for the default entry ────────

TEST_CASE("SettingsMedia: ok() persists the selected device ids",
          "[qt][settingsmedia]") {
    ensureSettings();
    auto *ws = qtCtx()->settings();

    SettingsMedia m;
    // Selecting the default entry (index 0) stores an empty device id.
    m.ok();
    REQUIRE(ws->getStr(WS_MEDIA_CAMERA_DEVICE).isEmpty());
    REQUIRE(ws->getStr(WS_MEDIA_MIC_DEVICE).isEmpty());
}
