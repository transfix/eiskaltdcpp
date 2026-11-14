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

#include "ChannelFrame.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QHeaderView>
#include <QTextBrowser>
#include <QLineEdit>
#include <QToolBar>
#include <QToolButton>
#include <QLabel>
#include <QMenu>
#include <QAction>
#include <QInputDialog>
#include <QMessageBox>
#include <QCloseEvent>
#include <QDateTime>
#include <QDebug>

// ChannelRole values MUST match dcpp::ChannelManager / nmdcpb.proto.
enum { ROLE_MEMBER = 0, ROLE_ADMIN = 1, ROLE_OWNER = 2, ROLE_READONLY = 3 };
// PbChannelMemberUpdate.UpdateType
enum { UPD_JOINED = 0, UPD_LEFT = 1, UPD_KICKED = 2, UPD_ROLE_CHANGED = 3 };

static QString roleName(int role)
{
    switch (role) {
        case ROLE_ADMIN:    return QObject::tr("Admin");
        case ROLE_OWNER:    return QObject::tr("Owner");
        case ROLE_READONLY: return QObject::tr("Read-only");
        case ROLE_MEMBER:
        default:            return QObject::tr("Member");
    }
}

static QString escapeHtml(const QString &s)
{
    QString out = s;
    out.replace('&', "&amp;").replace('<', "&lt;").replace('>', "&gt;");
    return out;
}

ChannelFrame::ChannelFrame(const QString &hubUrl, const QString &hubName)
    : hubUrl_(hubUrl), hubName_(hubName)
{
    setAttribute(Qt::WA_DeleteOnClose);
    buildUi();
    updateActionState();
}

ChannelFrame::~ChannelFrame() = default;

QString ChannelFrame::getArenaTitle()
{
    return tr("Channels") + " :: " + (hubName_.isEmpty() ? hubUrl_ : hubName_);
}

QString ChannelFrame::getArenaShortTitle()
{
    return tr("Channels");
}

void ChannelFrame::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(2, 2, 2, 2);
    root->setSpacing(2);

    // --- toolbar ---
    auto *toolbar = new QToolBar(this);
    actRefresh_ = toolbar->addAction(tr("Refresh"));
    actCreate_  = toolbar->addAction(tr("Create…"));
    actJoin_    = toolbar->addAction(tr("Join"));
    actLeave_   = toolbar->addAction(tr("Leave"));
    root->addWidget(toolbar);

    connect(actRefresh_, &QAction::triggered, this, &ChannelFrame::slotRefresh);
    connect(actCreate_,  &QAction::triggered, this, &ChannelFrame::slotCreate);
    connect(actJoin_,    &QAction::triggered, this, &ChannelFrame::slotJoinSelected);
    connect(actLeave_,   &QAction::triggered, this, &ChannelFrame::slotLeaveSelected);

    // --- main splitter ---
    splitter_ = new QSplitter(Qt::Horizontal, this);

    channelTree_ = new QTreeWidget(splitter_);
    channelTree_->setColumnCount(3);
    channelTree_->setHeaderLabels({ tr("Channel"), tr("Members"), tr("Topic") });
    channelTree_->setRootIsDecorated(false);
    channelTree_->setSelectionMode(QAbstractItemView::SingleSelection);
    channelTree_->setContextMenuPolicy(Qt::CustomContextMenu);
    channelTree_->header()->setStretchLastSection(true);
    splitter_->addWidget(channelTree_);

    connect(channelTree_, &QTreeWidget::itemSelectionChanged,
            this, &ChannelFrame::slotChannelSelected);
    connect(channelTree_, &QTreeWidget::customContextMenuRequested,
            this, &ChannelFrame::slotChannelContextMenu);
    connect(channelTree_, &QTreeWidget::itemDoubleClicked,
            this, [this](QTreeWidgetItem *, int) { slotJoinSelected(); });

    // --- chat side ---
    auto *chatPane = new QWidget(splitter_);
    auto *chatLay = new QVBoxLayout(chatPane);
    chatLay->setContentsMargins(0, 0, 0, 0);
    chatLay->setSpacing(2);

    chatHeader_ = new QLabel(tr("Select a channel"), chatPane);
    chatHeader_->setStyleSheet("font-weight: bold; padding: 2px;");
    chatLay->addWidget(chatHeader_);

    chatView_ = new QTextBrowser(chatPane);
    chatView_->setOpenExternalLinks(true);
    chatLay->addWidget(chatView_, 1);

    auto *inputLay = new QHBoxLayout();
    inputLay->setContentsMargins(0, 0, 0, 0);
    input_ = new QLineEdit(chatPane);
    input_->setPlaceholderText(tr("Type a message…"));
    inputLay->addWidget(input_, 1);
    sendBtn_ = new QToolButton(chatPane);
    sendBtn_->setText(tr("Send"));
    inputLay->addWidget(sendBtn_);
    chatLay->addLayout(inputLay);

    connect(input_,   &QLineEdit::returnPressed, this, &ChannelFrame::slotSend);
    connect(sendBtn_, &QToolButton::clicked,     this, &ChannelFrame::slotSend);

    splitter_->addWidget(chatPane);
    splitter_->setStretchFactor(0, 1);
    splitter_->setStretchFactor(1, 2);

    root->addWidget(splitter_, 1);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

QString ChannelFrame::selectedChannelId() const
{
    auto items = channelTree_->selectedItems();
    if (items.isEmpty())
        return QString();
    return items.first()->data(0, Qt::UserRole).toString();
}

ChannelRow *ChannelFrame::findRow(const QString &channelId)
{
    for (auto &r : channels_)
        if (r.id == channelId)
            return &r;
    return nullptr;
}

QTreeWidgetItem *ChannelFrame::itemForChannel(const QString &channelId) const
{
    for (int i = 0; i < channelTree_->topLevelItemCount(); ++i) {
        auto *it = channelTree_->topLevelItem(i);
        if (it->data(0, Qt::UserRole).toString() == channelId)
            return it;
    }
    return nullptr;
}

void ChannelFrame::refreshTreeItem(QTreeWidgetItem *item, const ChannelRow &row)
{
    QString name = row.name.isEmpty() ? row.id : row.name;
    if (row.isPrivate)
        name = QString::fromUtf8("\xF0\x9F\x94\x92 ") + name; // 🔒
    item->setText(0, name);
    item->setText(1, QString::number(row.memberCount));
    item->setText(2, row.topic);
    item->setData(0, Qt::UserRole, row.id);
    QFont f = item->font(0);
    f.setBold(row.joined);
    item->setFont(0, f);
}

void ChannelFrame::updateActionState()
{
    const QString id = selectedChannelId();
    ChannelRow *row = id.isEmpty() ? nullptr : findRow(id);
    const bool hasSel = row != nullptr;
    actJoin_->setEnabled(hasSel && !row->joined);
    actLeave_->setEnabled(hasSel && row->joined);

    const bool chatEnabled = hasSel && row->joined;
    input_->setEnabled(chatEnabled);
    sendBtn_->setEnabled(chatEnabled);
}

void ChannelFrame::appendChat(const QString &html)
{
    chatView_->append(html);
}

// ---------------------------------------------------------------------------
// State pushed by HubFrame
// ---------------------------------------------------------------------------

void ChannelFrame::setChannelList(const QList<ChannelRow> &channels)
{
    const QString sel = selectedChannelId();
    channels_ = channels;

    channelTree_->clear();
    for (const auto &row : channels_) {
        auto *item = new QTreeWidgetItem(channelTree_);
        refreshTreeItem(item, row);
        if (row.id == sel)
            item->setSelected(true);
    }
    updateActionState();
}

void ChannelFrame::channelCreated(const QString &channelId, const QString &ownerNick, bool isPrivate)
{
    ChannelRow *row = findRow(channelId);
    if (!row) {
        ChannelRow nr;
        nr.id = channelId;
        nr.name = channelId;
        nr.owner = ownerNick;
        nr.isPrivate = isPrivate;
        channels_.append(nr);
        auto *item = new QTreeWidgetItem(channelTree_);
        refreshTreeItem(item, nr);
    } else {
        row->owner = ownerNick;
        row->isPrivate = isPrivate;
        if (auto *it = itemForChannel(channelId))
            refreshTreeItem(it, *row);
    }
    channelStatus(channelId, tr("Channel created by %1").arg(ownerNick));
    updateActionState();
}

void ChannelFrame::memberUpdate(const QString &channelId, const QString &nick,
                                int updateType, int newRole, const QString &reason)
{
    ChannelRow *row = findRow(channelId);
    if (row) {
        switch (updateType) {
            case UPD_JOINED: row->memberCount += 1; break;
            case UPD_LEFT:
            case UPD_KICKED: if (row->memberCount > 0) row->memberCount -= 1; break;
            default: break;
        }
        if (auto *it = itemForChannel(channelId))
            refreshTreeItem(it, *row);
    }

    QString msg;
    switch (updateType) {
        case UPD_JOINED:       msg = tr("%1 joined").arg(nick); break;
        case UPD_LEFT:         msg = tr("%1 left").arg(nick); break;
        case UPD_KICKED:       msg = reason.isEmpty()
                                     ? tr("%1 was kicked").arg(nick)
                                     : tr("%1 was kicked (%2)").arg(nick, reason); break;
        case UPD_ROLE_CHANGED: msg = tr("%1 is now %2").arg(nick, roleName(newRole)); break;
        default:               msg = tr("%1 updated").arg(nick); break;
    }
    channelStatus(channelId, msg);
    updateActionState();
}

void ChannelFrame::channelMessage(const QString &channelId, const QString &fromNick,
                                  const QString &text, bool isAction, quint64 timestamp)
{
    if (channelId != openChatChannel_ && channelId != selectedChannelId())
        return;

    const QString ts = timestamp
        ? QDateTime::fromMSecsSinceEpoch((qint64)timestamp).toString("hh:mm:ss")
        : QDateTime::currentDateTime().toString("hh:mm:ss");

    QString line;
    if (isAction) {
        line = QString("<span style='color:gray'>[%1]</span> * <b>%2</b> %3")
                   .arg(ts, escapeHtml(fromNick), escapeHtml(text));
    } else {
        line = QString("<span style='color:gray'>[%1]</span> &lt;<b>%2</b>&gt; %3")
                   .arg(ts, escapeHtml(fromNick), escapeHtml(text));
    }
    appendChat(line);
}

void ChannelFrame::channelInvite(const QString &channelId, const QString &inviterNick)
{
    const QString title = tr("Channel invitation");
    const QString msg = tr("%1 invited you to channel \"%2\".\n\nJoin now?")
                            .arg(inviterNick, channelId);
    const auto ret = QMessageBox::question(this, title, msg,
                                           QMessageBox::Yes | QMessageBox::No);
    const bool accepted = (ret == QMessageBox::Yes);
    emit inviteResponse(channelId, accepted);
    if (accepted)
        emit joinRequested(channelId);
}

void ChannelFrame::channelStatus(const QString &channelId, const QString &text)
{
    if (channelId != openChatChannel_ && channelId != selectedChannelId())
        return;
    appendChat(QString("<span style='color:gray'>*** %1</span>").arg(escapeHtml(text)));
}

// ---------------------------------------------------------------------------
// User actions
// ---------------------------------------------------------------------------

void ChannelFrame::slotChannelSelected()
{
    const QString id = selectedChannelId();
    ChannelRow *row = id.isEmpty() ? nullptr : findRow(id);

    if (id != openChatChannel_) {
        openChatChannel_ = id;
        chatView_->clear();
    }

    if (row) {
        QString hdr = (row->name.isEmpty() ? row->id : row->name);
        if (!row->topic.isEmpty())
            hdr += " — " + row->topic;
        if (row->isPrivate)
            hdr += tr("  [private]");
        chatHeader_->setText(hdr);
    } else {
        chatHeader_->setText(tr("Select a channel"));
    }
    updateActionState();
}

void ChannelFrame::slotCreate()
{
    bool ok = false;
    const QString id = QInputDialog::getText(this, tr("Create channel"),
                                             tr("Channel id:"), QLineEdit::Normal,
                                             QString(), &ok);
    if (!ok || id.trimmed().isEmpty())
        return;
    const QString name = QInputDialog::getText(this, tr("Create channel"),
                                               tr("Display name:"), QLineEdit::Normal,
                                               id, &ok);
    if (!ok)
        return;
    const QString topic = QInputDialog::getText(this, tr("Create channel"),
                                                tr("Topic (optional):"), QLineEdit::Normal,
                                                QString(), &ok);
    if (!ok)
        return;
    const auto priv = QMessageBox::question(this, tr("Create channel"),
                                            tr("Make this channel private (encrypted)?"),
                                            QMessageBox::Yes | QMessageBox::No);
    emit createRequested(id.trimmed(), name.trimmed(), topic.trimmed(),
                         priv == QMessageBox::Yes);
}

void ChannelFrame::slotJoinSelected()
{
    const QString id = selectedChannelId();
    if (!id.isEmpty())
        emit joinRequested(id);
}

void ChannelFrame::slotLeaveSelected()
{
    const QString id = selectedChannelId();
    if (!id.isEmpty())
        emit leaveRequested(id);
}

void ChannelFrame::slotRefresh()
{
    emit refreshRequested();
}

void ChannelFrame::slotSend()
{
    const QString id = selectedChannelId();
    QString text = input_->text();
    if (id.isEmpty() || text.isEmpty())
        return;

    bool isAction = false;
    if (text.startsWith("/me ")) {
        isAction = true;
        text = text.mid(4);
    }
    emit sendMessageRequested(id, text, isAction);
    input_->clear();
}

void ChannelFrame::slotChannelContextMenu(const QPoint &pos)
{
    QTreeWidgetItem *item = channelTree_->itemAt(pos);
    if (!item)
        return;
    const QString id = item->data(0, Qt::UserRole).toString();
    ChannelRow *row = findRow(id);
    if (!row)
        return;

    QMenu menu(this);
    QAction *aJoin  = menu.addAction(tr("Join"));
    QAction *aLeave = menu.addAction(tr("Leave"));
    aJoin->setEnabled(!row->joined);
    aLeave->setEnabled(row->joined);
    menu.addSeparator();
    QAction *aTopic  = menu.addAction(tr("Set topic…"));
    QAction *aInvite = menu.addAction(tr("Invite user…"));
    QAction *aKick   = menu.addAction(tr("Kick user…"));

    QMenu *roleMenu = menu.addMenu(tr("Set user role"));
    QAction *aRoleMember = roleMenu->addAction(tr("Member"));
    QAction *aRoleAdmin  = roleMenu->addAction(tr("Admin"));
    QAction *aRoleRead   = roleMenu->addAction(tr("Read-only"));

    QAction *chosen = menu.exec(channelTree_->viewport()->mapToGlobal(pos));
    if (!chosen)
        return;

    if (chosen == aJoin) {
        emit joinRequested(id);
    } else if (chosen == aLeave) {
        emit leaveRequested(id);
    } else if (chosen == aTopic) {
        bool ok = false;
        const QString t = QInputDialog::getText(this, tr("Set topic"), tr("Topic:"),
                                                QLineEdit::Normal, row->topic, &ok);
        if (ok)
            emit setTopicRequested(id, t);
    } else if (chosen == aInvite) {
        bool ok = false;
        const QString nick = QInputDialog::getText(this, tr("Invite user"), tr("Nick:"),
                                                   QLineEdit::Normal, QString(), &ok);
        if (ok && !nick.trimmed().isEmpty())
            emit inviteRequested(id, nick.trimmed());
    } else if (chosen == aKick) {
        bool ok = false;
        const QString nick = QInputDialog::getText(this, tr("Kick user"), tr("Nick:"),
                                                   QLineEdit::Normal, QString(), &ok);
        if (ok && !nick.trimmed().isEmpty())
            emit kickRequested(id, nick.trimmed());
    } else if (chosen == aRoleMember || chosen == aRoleAdmin || chosen == aRoleRead) {
        bool ok = false;
        const QString nick = QInputDialog::getText(this, tr("Set user role"), tr("Nick:"),
                                                   QLineEdit::Normal, QString(), &ok);
        if (ok && !nick.trimmed().isEmpty()) {
            int r = ROLE_MEMBER;
            if (chosen == aRoleAdmin) r = ROLE_ADMIN;
            else if (chosen == aRoleRead) r = ROLE_READONLY;
            emit setRoleRequested(id, nick.trimmed(), r);
        }
    }
}

void ChannelFrame::closeEvent(QCloseEvent *e)
{
    e->accept();
}
