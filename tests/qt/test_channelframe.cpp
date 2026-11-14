/*
 * Tests for ChannelFrame — the NMDCpb channels arena widget (Qt UI).
 *
 * ChannelFrame is intentionally UI-only: HubFrame pushes state in through
 * public slots and ChannelFrame emits *Requested signals for user actions.
 * These tests drive the state-push slots and observe the resulting widget
 * state, and trigger the toolbar actions to verify the outgoing signals.
 *
 * Pure Qt (no live hub); runs headless with QT_QPA_PLATFORM=offscreen.
 */

#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QTreeWidget>
#include <QTextBrowser>
#include <QLineEdit>
#include <QToolButton>
#include <QLabel>
#include <QAction>
#include <QSignalSpy>

#include "ChannelFrame.h"

// UpdateType / role values mirrored from dcpp::ChannelManager / nmdcpb.proto.
namespace {
constexpr int UPD_JOINED = 0;
constexpr int UPD_LEFT   = 1;
constexpr int UPD_KICKED = 2;

constexpr int ROLE_MEMBER = 0;

ChannelRow makeRow(const QString &id, const QString &name, int members,
                   bool joined = false, bool priv = false)
{
    ChannelRow r;
    r.id = id;
    r.name = name;
    r.topic = "";
    r.isPrivate = priv;
    r.owner = "owner";
    r.memberCount = members;
    r.joined = joined;
    return r;
}

QTreeWidget *treeOf(ChannelFrame &f)
{
    return f.findChild<QTreeWidget *>();
}

QTextBrowser *chatOf(ChannelFrame &f)
{
    return f.findChild<QTextBrowser *>();
}

QLineEdit *inputOf(ChannelFrame &f)
{
    return f.findChild<QLineEdit *>();
}
} // namespace

// ─── Construction ───────────────────────────────────────────────────────

TEST_CASE("ChannelFrame: constructs with hub identity", "[qt][channelframe]") {
    ChannelFrame f("adc://hub.example:411", "Example Hub");
    REQUIRE(f.hubUrl() == QString("adc://hub.example:411"));
    REQUIRE(f.role() == ArenaWidget::Channel);
    REQUIRE(f.getArenaShortTitle() == QString("Channels"));
    REQUIRE(f.getArenaTitle().contains("Example Hub"));
    // an empty tree to start with
    REQUIRE(treeOf(f) != nullptr);
    REQUIRE(treeOf(f)->topLevelItemCount() == 0);
}

// ─── setChannelList ─────────────────────────────────────────────────────

TEST_CASE("ChannelFrame: setChannelList populates the tree", "[qt][channelframe]") {
    ChannelFrame f("adc://h", "H");

    QList<ChannelRow> list;
    list << makeRow("general", "General", 5)
         << makeRow("dev", "Dev", 2, /*joined=*/true)
         << makeRow("secret", "Secret", 1, /*joined=*/false, /*priv=*/true);
    f.setChannelList(list);

    auto *tree = treeOf(f);
    REQUIRE(tree->topLevelItemCount() == 3);
    // member count column
    REQUIRE(tree->topLevelItem(0)->text(1) == QString("5"));
    // private channel gets a lock prefix on the name
    REQUIRE(tree->topLevelItem(2)->text(0).contains("Secret"));
    // channel id round-trips through UserRole
    REQUIRE(tree->topLevelItem(1)->data(0, Qt::UserRole).toString() == QString("dev"));
}

TEST_CASE("ChannelFrame: setChannelList replaces previous contents", "[qt][channelframe]") {
    ChannelFrame f("adc://h", "H");
    f.setChannelList({ makeRow("a", "A", 1), makeRow("b", "B", 1) });
    REQUIRE(treeOf(f)->topLevelItemCount() == 2);

    f.setChannelList({ makeRow("c", "C", 1) });
    REQUIRE(treeOf(f)->topLevelItemCount() == 1);
    REQUIRE(treeOf(f)->topLevelItem(0)->data(0, Qt::UserRole).toString() == QString("c"));
}

// ─── channelCreated ─────────────────────────────────────────────────────

TEST_CASE("ChannelFrame: channelCreated adds a new row", "[qt][channelframe]") {
    ChannelFrame f("adc://h", "H");
    f.channelCreated("newchan", "alice", /*isPrivate=*/true);

    auto *tree = treeOf(f);
    REQUIRE(tree->topLevelItemCount() == 1);
    REQUIRE(tree->topLevelItem(0)->data(0, Qt::UserRole).toString() == QString("newchan"));
}

// ─── memberUpdate maintains member count ────────────────────────────────

TEST_CASE("ChannelFrame: memberUpdate adjusts the member count", "[qt][channelframe]") {
    ChannelFrame f("adc://h", "H");
    f.setChannelList({ makeRow("general", "General", 5) });
    auto *tree = treeOf(f);

    f.memberUpdate("general", "bob", UPD_JOINED, ROLE_MEMBER, "");
    REQUIRE(tree->topLevelItem(0)->text(1) == QString("6"));

    f.memberUpdate("general", "bob", UPD_LEFT, ROLE_MEMBER, "");
    REQUIRE(tree->topLevelItem(0)->text(1) == QString("5"));

    f.memberUpdate("general", "eve", UPD_KICKED, ROLE_MEMBER, "spam");
    REQUIRE(tree->topLevelItem(0)->text(1) == QString("4"));
}

TEST_CASE("ChannelFrame: memberUpdate never goes below zero", "[qt][channelframe]") {
    ChannelFrame f("adc://h", "H");
    f.setChannelList({ makeRow("general", "General", 0) });
    f.memberUpdate("general", "bob", UPD_LEFT, ROLE_MEMBER, "");
    REQUIRE(treeOf(f)->topLevelItem(0)->text(1) == QString("0"));
}

// ─── channelMessage renders into the chat view ──────────────────────────

TEST_CASE("ChannelFrame: channelMessage appends to the open channel chat", "[qt][channelframe]") {
    ChannelFrame f("adc://h", "H");
    f.setChannelList({ makeRow("general", "General", 1, /*joined=*/true) });

    // Select the channel so it becomes the open chat.
    treeOf(f)->topLevelItem(0)->setSelected(true);

    f.channelMessage("general", "alice", "hello world", /*isAction=*/false, 0);
    REQUIRE(chatOf(f)->toPlainText().contains("hello world"));
    REQUIRE(chatOf(f)->toPlainText().contains("alice"));
}

TEST_CASE("ChannelFrame: channelMessage for other channel is ignored", "[qt][channelframe]") {
    ChannelFrame f("adc://h", "H");
    f.setChannelList({ makeRow("general", "General", 1, true),
                       makeRow("other", "Other", 1, true) });
    treeOf(f)->topLevelItem(0)->setSelected(true); // open "general"

    f.channelMessage("other", "bob", "not shown here", false, 0);
    REQUIRE_FALSE(chatOf(f)->toPlainText().contains("not shown here"));
}

TEST_CASE("ChannelFrame: channelMessage escapes HTML", "[qt][channelframe]") {
    ChannelFrame f("adc://h", "H");
    f.setChannelList({ makeRow("general", "General", 1, true) });
    treeOf(f)->topLevelItem(0)->setSelected(true);

    f.channelMessage("general", "alice", "<script>x</script>", false, 0);
    // The literal tag text must survive as visible text (escaped), not markup.
    REQUIRE(chatOf(f)->toPlainText().contains("<script>x</script>"));
}

// ─── User-action signals ────────────────────────────────────────────────

TEST_CASE("ChannelFrame: Refresh action emits refreshRequested", "[qt][channelframe]") {
    ChannelFrame f("adc://h", "H");
    QSignalSpy spy(&f, &ChannelFrame::refreshRequested);

    auto *refresh = f.findChild<QAction *>();
    // The first QAction created is Refresh (see buildUi order); guard anyway.
    REQUIRE(refresh != nullptr);
    // Trigger every action named to find Refresh robustly.
    for (auto *a : f.findChildren<QAction *>()) {
        if (a->text().contains("Refresh")) {
            a->trigger();
            break;
        }
    }
    REQUIRE(spy.count() == 1);
}

TEST_CASE("ChannelFrame: selecting a joined channel enables input", "[qt][channelframe]") {
    ChannelFrame f("adc://h", "H");
    f.setChannelList({ makeRow("general", "General", 1, /*joined=*/true) });
    treeOf(f)->topLevelItem(0)->setSelected(true);
    REQUIRE(inputOf(f)->isEnabled());
}

TEST_CASE("ChannelFrame: sending a message emits sendMessageRequested", "[qt][channelframe]") {
    ChannelFrame f("adc://h", "H");
    f.setChannelList({ makeRow("general", "General", 1, /*joined=*/true) });
    treeOf(f)->topLevelItem(0)->setSelected(true);

    QSignalSpy spy(&f, &ChannelFrame::sendMessageRequested);
    inputOf(f)->setText("hi there");
    // returnPressed is wired to slotSend
    emit inputOf(f)->returnPressed();

    REQUIRE(spy.count() == 1);
    const auto args = spy.takeFirst();
    REQUIRE(args.at(0).toString() == QString("general"));
    REQUIRE(args.at(1).toString() == QString("hi there"));
    REQUIRE(args.at(2).toBool() == false);
    // input cleared after send
    REQUIRE(inputOf(f)->text().isEmpty());
}

TEST_CASE("ChannelFrame: /me prefix sends an action message", "[qt][channelframe]") {
    ChannelFrame f("adc://h", "H");
    f.setChannelList({ makeRow("general", "General", 1, true) });
    treeOf(f)->topLevelItem(0)->setSelected(true);

    QSignalSpy spy(&f, &ChannelFrame::sendMessageRequested);
    inputOf(f)->setText("/me waves");
    emit inputOf(f)->returnPressed();

    REQUIRE(spy.count() == 1);
    const auto args = spy.takeFirst();
    REQUIRE(args.at(1).toString() == QString("waves"));
    REQUIRE(args.at(2).toBool() == true);
}

TEST_CASE("ChannelFrame: empty message is not sent", "[qt][channelframe]") {
    ChannelFrame f("adc://h", "H");
    f.setChannelList({ makeRow("general", "General", 1, true) });
    treeOf(f)->topLevelItem(0)->setSelected(true);

    QSignalSpy spy(&f, &ChannelFrame::sendMessageRequested);
    inputOf(f)->setText("");
    emit inputOf(f)->returnPressed();
    REQUIRE(spy.count() == 0);
}
