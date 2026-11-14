/*
 * Tests for CallFrame — the NMDCpb voice/video calls arena widget (Qt UI).
 *
 * CallFrame is UI-only: HubFrame pushes signaling state in through public
 * slots and CallFrame emits *Requested signals for user actions. These tests
 * drive the state-push slots and observe the resulting widget state, and
 * trigger the toolbar/control actions to verify the outgoing signals.
 *
 * Pure Qt (no live hub); runs headless with QT_QPA_PLATFORM=offscreen.
 */

#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QTreeWidget>
#include <QTextBrowser>
#include <QToolButton>
#include <QGroupBox>
#include <QLabel>
#include <QAction>
#include <QSignalSpy>

#include "CallFrame.h"

namespace {

CallRow makeRow(const QString &id, const QString &peer, int state,
                bool hasVideo = false)
{
    CallRow r;
    r.id = id;
    r.peer = peer;
    r.isGroup = false;
    r.state = state;
    r.hasVideo = hasVideo;
    return r;
}

QTreeWidget *treeOf(CallFrame &f)
{
    return f.findChild<QTreeWidget *>();
}

QTextBrowser *statusOf(CallFrame &f)
{
    return f.findChild<QTextBrowser *>();
}

// Find a QAction by its (untranslated) text.
QAction *actionByText(CallFrame &f, const QString &text)
{
    for (QAction *a : f.findChildren<QAction *>())
        if (a->text() == text)
            return a;
    return nullptr;
}

// Find a checkable QToolButton by its text.
QToolButton *buttonByText(CallFrame &f, const QString &text)
{
    for (QToolButton *b : f.findChildren<QToolButton *>())
        if (b->text() == text)
            return b;
    return nullptr;
}

void select(CallFrame &f, int row)
{
    treeOf(f)->topLevelItem(row)->setSelected(true);
}

} // namespace

// ─── Construction ───────────────────────────────────────────────────────

TEST_CASE("CallFrame: constructs with hub identity", "[qt][callframe]") {
    CallFrame f("adc://hub.example:411", "Example Hub");
    REQUIRE(f.hubUrl() == QString("adc://hub.example:411"));
    REQUIRE(f.role() == ArenaWidget::Call);
    REQUIRE(f.getArenaShortTitle() == QString("Calls"));
    REQUIRE(f.getArenaTitle().contains("Example Hub"));
    REQUIRE(treeOf(f) != nullptr);
    REQUIRE(treeOf(f)->topLevelItemCount() == 0);
}

// ─── setCallList ────────────────────────────────────────────────────────

TEST_CASE("CallFrame: setCallList populates the tree", "[qt][callframe]") {
    CallFrame f("adc://h", "H");

    QList<CallRow> list;
    list << makeRow("c1", "alice", CallFrame::ST_ACTIVE, /*hasVideo=*/true)
         << makeRow("c2", "bob", CallFrame::ST_RINGING_OUT);
    f.setCallList(list);

    auto *tree = treeOf(f);
    REQUIRE(tree->topLevelItemCount() == 2);
    REQUIRE(tree->topLevelItem(0)->text(0) == QString("alice"));
    REQUIRE(tree->topLevelItem(0)->data(0, Qt::UserRole).toString() == QString("c1"));
    // media column reflects video negotiation
    REQUIRE(tree->topLevelItem(0)->text(2).contains("Video"));
    REQUIRE_FALSE(tree->topLevelItem(1)->text(2).contains("Video"));
}

TEST_CASE("CallFrame: setCallList replaces previous contents", "[qt][callframe]") {
    CallFrame f("adc://h", "H");
    f.setCallList({ makeRow("a", "A", CallFrame::ST_ACTIVE),
                    makeRow("b", "B", CallFrame::ST_ACTIVE) });
    REQUIRE(treeOf(f)->topLevelItemCount() == 2);

    f.setCallList({ makeRow("c", "C", CallFrame::ST_ACTIVE) });
    REQUIRE(treeOf(f)->topLevelItemCount() == 1);
    REQUIRE(treeOf(f)->topLevelItem(0)->data(0, Qt::UserRole).toString() == QString("c"));
}

// ─── callOffer ──────────────────────────────────────────────────────────

TEST_CASE("CallFrame: callOffer adds a ringing-in row", "[qt][callframe]") {
    CallFrame f("adc://h", "H");
    QList<int> media;
    media << CallFrame::MEDIA_AUDIO << CallFrame::MEDIA_VIDEO;
    f.callOffer("call-9", "carol", /*isGroup=*/false, media);

    auto *tree = treeOf(f);
    REQUIRE(tree->topLevelItemCount() == 1);
    REQUIRE(tree->topLevelItem(0)->data(0, Qt::UserRole).toString() == QString("call-9"));
    REQUIRE(tree->topLevelItem(0)->text(0) == QString("carol"));
    REQUIRE(tree->topLevelItem(0)->text(2).contains("Video"));
    // status log mentions the incoming call
    REQUIRE(statusOf(f)->toPlainText().contains("carol"));
}

TEST_CASE("CallFrame: callOffer escapes HTML in the peer nick", "[qt][callframe]") {
    CallFrame f("adc://h", "H");
    f.callOffer("c", "<b>x</b>", false, { CallFrame::MEDIA_AUDIO });
    // rendered as text, not markup
    REQUIRE(statusOf(f)->toPlainText().contains("<b>x</b>"));
}

// ─── callAnswer transitions state ───────────────────────────────────────

TEST_CASE("CallFrame: callAnswer accepted moves call to active", "[qt][callframe]") {
    CallFrame f("adc://h", "H");
    f.setCallList({ makeRow("c1", "alice", CallFrame::ST_RINGING_OUT) });
    f.callAnswer("c1", "alice", /*accepted=*/true, "");

    REQUIRE(treeOf(f)->topLevelItem(0)->text(1) == QString("Active"));
    REQUIRE(statusOf(f)->toPlainText().contains("accepted"));
}

TEST_CASE("CallFrame: callAnswer rejected ends the call", "[qt][callframe]") {
    CallFrame f("adc://h", "H");
    f.setCallList({ makeRow("c1", "alice", CallFrame::ST_RINGING_OUT) });
    f.callAnswer("c1", "alice", /*accepted=*/false, "busy");

    REQUIRE(treeOf(f)->topLevelItem(0)->text(1) == QString("Ended"));
    REQUIRE(statusOf(f)->toPlainText().contains("busy"));
}

// ─── callEnded ──────────────────────────────────────────────────────────

TEST_CASE("CallFrame: callEnded marks the call ended", "[qt][callframe]") {
    CallFrame f("adc://h", "H");
    f.setCallList({ makeRow("c1", "alice", CallFrame::ST_ACTIVE) });
    f.callEnded("c1", /*reason=*/0, /*durationSec=*/42);

    REQUIRE(treeOf(f)->topLevelItem(0)->text(1) == QString("Ended"));
    REQUIRE(statusOf(f)->toPlainText().contains("42"));
}

// ─── callMediaControl records remote state ──────────────────────────────

TEST_CASE("CallFrame: callMediaControl logs remote media changes", "[qt][callframe]") {
    CallFrame f("adc://h", "H");
    f.setCallList({ makeRow("c1", "alice", CallFrame::ST_ACTIVE, true) });
    f.callMediaControl("c1", "alice", /*audioMuted=*/true, /*videoMuted=*/false,
                       /*screenSharing=*/true);
    const QString log = statusOf(f)->toPlainText();
    REQUIRE(log.contains("alice"));
    REQUIRE(log.contains("mic off"));
    REQUIRE(log.contains("sharing screen"));
}

// ─── Outgoing signals ───────────────────────────────────────────────────

TEST_CASE("CallFrame: answer action emits answerRequested(accepted)", "[qt][callframe]") {
    CallFrame f("adc://h", "H");
    f.setCallList({ makeRow("c1", "alice", CallFrame::ST_RINGING_IN) });
    select(f, 0);

    QSignalSpy spy(&f, &CallFrame::answerRequested);
    QAction *answer = actionByText(f, "Answer");
    REQUIRE(answer != nullptr);
    REQUIRE(answer->isEnabled());
    answer->trigger();

    REQUIRE(spy.count() == 1);
    REQUIRE(spy.at(0).at(0).toString() == QString("c1"));
    REQUIRE(spy.at(0).at(1).toBool() == true);
}

TEST_CASE("CallFrame: reject action emits answerRequested(rejected)", "[qt][callframe]") {
    CallFrame f("adc://h", "H");
    f.setCallList({ makeRow("c1", "alice", CallFrame::ST_RINGING_IN) });
    select(f, 0);

    QSignalSpy spy(&f, &CallFrame::answerRequested);
    QAction *reject = actionByText(f, "Reject");
    REQUIRE(reject != nullptr);
    reject->trigger();

    REQUIRE(spy.count() == 1);
    REQUIRE(spy.at(0).at(1).toBool() == false);
}

TEST_CASE("CallFrame: hangup action emits endRequested", "[qt][callframe]") {
    CallFrame f("adc://h", "H");
    f.setCallList({ makeRow("c1", "alice", CallFrame::ST_ACTIVE) });
    select(f, 0);

    QSignalSpy spy(&f, &CallFrame::endRequested);
    QAction *hangup = actionByText(f, "Hang Up");
    REQUIRE(hangup != nullptr);
    hangup->trigger();

    REQUIRE(spy.count() == 1);
    REQUIRE(spy.at(0).at(0).toString() == QString("c1"));
}

TEST_CASE("CallFrame: mic toggle emits mediaControlRequested", "[qt][callframe]") {
    CallFrame f("adc://h", "H");
    f.setCallList({ makeRow("c1", "alice", CallFrame::ST_ACTIVE) });
    select(f, 0);

    QSignalSpy spy(&f, &CallFrame::mediaControlRequested);
    QToolButton *mic = buttonByText(f, "Mute Mic");
    REQUIRE(mic != nullptr);
    REQUIRE(mic->isEnabled());
    mic->click(); // toggles to checked → muted

    REQUIRE(spy.count() == 1);
    REQUIRE(spy.at(0).at(0).toString() == QString("c1"));
    REQUIRE(spy.at(0).at(1).toBool() == true);  // audioMuted
}

TEST_CASE("CallFrame: controls disabled without an active call", "[qt][callframe]") {
    CallFrame f("adc://h", "H");
    f.setCallList({ makeRow("c1", "alice", CallFrame::ST_RINGING_OUT) });
    select(f, 0);

    // mic control only makes sense on an active call
    QToolButton *mic = buttonByText(f, "Mute Mic");
    REQUIRE(mic != nullptr);
    REQUIRE_FALSE(mic->isEnabled());
}
