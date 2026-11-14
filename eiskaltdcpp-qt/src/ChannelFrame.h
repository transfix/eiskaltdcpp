/***************************************************************************
*                                                                         *
*   This program is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License as published by  *
*   the Free Software Foundation; either version 3 of the License, or     *
*   (at your option) any later version.                                   *
*                                                                         *
***************************************************************************/
/*
 * Copyright (C) 2026 Joe Rivera <transfix@sublevels.net>
 */

#pragma once

#include <QWidget>
#include <QMenu>
#include <QAction>
#include <QString>
#include <QList>
#include <QMetaType>

#include "ArenaWidget.h"

class QTreeWidget;
class QTreeWidgetItem;
class QTextBrowser;
class QLineEdit;
class QToolButton;
class QLabel;
class QSplitter;
class QCloseEvent;

/// A lightweight, Qt-friendly view of a channel (used for cross-thread signals
/// so we never pass dcpp/std types through queued connections).
struct ChannelRow {
    QString id;
    QString name;
    QString topic;
    bool isPrivate = false;
    QString owner;
    int memberCount = 0;
    bool joined = false;
};
Q_DECLARE_METATYPE(ChannelRow)
Q_DECLARE_METATYPE(QList<ChannelRow>)

/// Arena widget that presents the NMDCpb channel list of a single hub and the
/// chat of the currently-selected channel. It is intentionally UI-only: it
/// emits *Requested signals for every user action and exposes public slots the
/// owning HubFrame calls (on the GUI thread) to push state in. HubFrame owns
/// the dcpp::ChannelManager wiring and the hub connection lifecycle.
class ChannelFrame : public QWidget, public ArenaWidget
{
    Q_OBJECT
    Q_INTERFACES(ArenaWidget)

public:
    explicit ChannelFrame(const QString &hubUrl, const QString &hubName);
    ~ChannelFrame() override;

    ChannelFrame(const ChannelFrame&) = delete;
    ChannelFrame& operator=(const ChannelFrame&) = delete;

    // ArenaWidget interface
    QWidget *getWidget() override { return this; }
    QString  getArenaTitle() override;
    QString  getArenaShortTitle() override;
    QMenu   *getMenu() override { return nullptr; }
    ArenaWidget::Role role() const override { return ArenaWidget::Channel; }

    QString hubUrl() const { return hubUrl_; }

public Q_SLOTS:
    // ---- State pushed in by HubFrame (GUI thread) ----
    void setChannelList(const QList<ChannelRow> &channels);
    void channelCreated(const QString &channelId, const QString &ownerNick, bool isPrivate);
    void memberUpdate(const QString &channelId, const QString &nick,
                      int updateType, int newRole, const QString &reason);
    void channelMessage(const QString &channelId, const QString &fromNick,
                        const QString &text, bool isAction, quint64 timestamp);
    void channelInvite(const QString &channelId, const QString &inviterNick);
    void channelStatus(const QString &channelId, const QString &text);

Q_SIGNALS:
    // ---- User actions (handled by HubFrame) ----
    void refreshRequested();
    void createRequested(const QString &channelId, const QString &name,
                         const QString &topic, bool isPrivate);
    void joinRequested(const QString &channelId);
    void leaveRequested(const QString &channelId);
    void sendMessageRequested(const QString &channelId, const QString &text,
                              bool isAction);
    void setTopicRequested(const QString &channelId, const QString &topic);
    void inviteRequested(const QString &channelId, const QString &nick);
    void kickRequested(const QString &channelId, const QString &nick);
    void setRoleRequested(const QString &channelId, const QString &nick, int role);
    void inviteResponse(const QString &channelId, bool accepted);

protected:
    void closeEvent(QCloseEvent *e) override;

private Q_SLOTS:
    void slotChannelSelected();
    void slotCreate();
    void slotJoinSelected();
    void slotLeaveSelected();
    void slotRefresh();
    void slotSend();
    void slotChannelContextMenu(const QPoint &pos);

private:
    void buildUi();
    void appendChat(const QString &html);
    void updateActionState();
    QString selectedChannelId() const;
    ChannelRow *findRow(const QString &channelId);
    QTreeWidgetItem *itemForChannel(const QString &channelId) const;
    void refreshTreeItem(QTreeWidgetItem *item, const ChannelRow &row);

    QString hubUrl_;
    QString hubName_;

    QList<ChannelRow> channels_;
    QString openChatChannel_; ///< channel whose chat is currently shown

    // widgets
    QSplitter   *splitter_    = nullptr;
    QTreeWidget *channelTree_ = nullptr;
    QLabel      *chatHeader_  = nullptr;
    QTextBrowser*chatView_    = nullptr;
    QLineEdit   *input_       = nullptr;
    QToolButton *sendBtn_     = nullptr;

    QAction *actCreate_  = nullptr;
    QAction *actJoin_    = nullptr;
    QAction *actLeave_   = nullptr;
    QAction *actRefresh_ = nullptr;
};

Q_DECLARE_METATYPE(ChannelFrame*)
