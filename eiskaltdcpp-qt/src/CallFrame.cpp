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

#include "CallFrame.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QHeaderView>
#include <QLabel>
#include <QGroupBox>
#include <QToolBar>
#include <QToolButton>
#include <QTextBrowser>
#include <QAction>
#include <QLineEdit>
#include <QInputDialog>
#include <QMessageBox>
#include <QCheckBox>
#include <QSignalBlocker>
#include <QCloseEvent>
#include <QDateTime>

static QString escapeHtml(const QString &s)
{
    QString out = s;
    out.replace('&', "&amp;").replace('<', "&lt;").replace('>', "&gt;");
    return out;
}

CallFrame::CallFrame(const QString &hubUrl, const QString &hubName)
    : hubUrl_(hubUrl), hubName_(hubName)
{
    setAttribute(Qt::WA_DeleteOnClose);
    buildUi();
    updateControls();
}

CallFrame::~CallFrame() = default;

QString CallFrame::getArenaTitle()
{
    return tr("Calls") + " :: " + (hubName_.isEmpty() ? hubUrl_ : hubName_);
}

QString CallFrame::getArenaShortTitle()
{
    return tr("Calls");
}

QString CallFrame::stateName(int state)
{
    switch (state) {
        case ST_RINGING_OUT: return tr("Ringing…");
        case ST_RINGING_IN:  return tr("Incoming");
        case ST_ACTIVE:      return tr("Active");
        case ST_ENDED:       return tr("Ended");
        default:             return tr("Unknown");
    }
}

QString CallFrame::mediaSummary(const CallRow &row)
{
    QStringList parts;
    parts << tr("Audio");
    if (row.hasVideo)
        parts << tr("Video");
    return parts.join(" + ");
}

void CallFrame::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(2, 2, 2, 2);
    root->setSpacing(2);

    // --- toolbar ---
    auto *toolbar = new QToolBar(this);
    actNew_    = toolbar->addAction(tr("New Call…"));
    actAnswer_ = toolbar->addAction(tr("Answer"));
    actReject_ = toolbar->addAction(tr("Reject"));
    actHangup_ = toolbar->addAction(tr("Hang Up"));
    root->addWidget(toolbar);

    connect(actNew_,    &QAction::triggered, this, &CallFrame::slotNewCall);
    connect(actAnswer_, &QAction::triggered, this, &CallFrame::slotAnswer);
    connect(actReject_, &QAction::triggered, this, &CallFrame::slotReject);
    connect(actHangup_, &QAction::triggered, this, &CallFrame::slotHangup);

    // --- main splitter: call list | detail ---
    auto *splitter = new QSplitter(Qt::Horizontal, this);

    callTree_ = new QTreeWidget(splitter);
    callTree_->setColumnCount(3);
    callTree_->setHeaderLabels({ tr("Peer"), tr("State"), tr("Media") });
    callTree_->setRootIsDecorated(false);
    callTree_->setSelectionMode(QAbstractItemView::SingleSelection);
    callTree_->header()->setStretchLastSection(true);
    splitter->addWidget(callTree_);

    connect(callTree_, &QTreeWidget::itemSelectionChanged,
            this, &CallFrame::slotCallSelected);

    // --- detail pane ---
    auto *detail = new QWidget(splitter);
    auto *dlay = new QVBoxLayout(detail);
    dlay->setContentsMargins(0, 0, 0, 0);
    dlay->setSpacing(2);

    header_ = new QLabel(tr("No call selected"), detail);
    header_->setStyleSheet("font-weight: bold; padding: 2px;");
    dlay->addWidget(header_);

    // video preview placeholder (real capture arrives with the media settings)
    videoArea_ = new QLabel(detail);
    videoArea_->setMinimumHeight(120);
    videoArea_->setAlignment(Qt::AlignCenter);
    videoArea_->setStyleSheet("background:#101014; color:#888; border:1px solid #333;");
    videoArea_->setText(tr("No video"));
    dlay->addWidget(videoArea_, 1);

    // in-call controls
    controls_ = new QGroupBox(tr("Controls"), detail);
    auto *clay = new QHBoxLayout(controls_);
    clay->setContentsMargins(4, 4, 4, 4);

    btnMic_ = new QToolButton(controls_);
    btnMic_->setCheckable(true);
    btnMic_->setText(tr("Mute Mic"));
    clay->addWidget(btnMic_);

    btnCamera_ = new QToolButton(controls_);
    btnCamera_->setCheckable(true);
    btnCamera_->setText(tr("Camera Off"));
    clay->addWidget(btnCamera_);

    btnScreen_ = new QToolButton(controls_);
    btnScreen_->setCheckable(true);
    btnScreen_->setText(tr("Share Screen"));
    clay->addWidget(btnScreen_);

    clay->addStretch(1);

    btnHangup_ = new QToolButton(controls_);
    btnHangup_->setText(tr("Hang Up"));
    clay->addWidget(btnHangup_);

    dlay->addWidget(controls_);

    connect(btnMic_,    &QToolButton::toggled, this, &CallFrame::slotToggleMic);
    connect(btnCamera_, &QToolButton::toggled, this, &CallFrame::slotToggleCamera);
    connect(btnScreen_, &QToolButton::toggled, this, &CallFrame::slotToggleScreenShare);
    connect(btnHangup_, &QToolButton::clicked, this, &CallFrame::slotHangup);

    statusView_ = new QTextBrowser(detail);
    statusView_->setMaximumHeight(120);
    dlay->addWidget(statusView_);

    splitter->addWidget(detail);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);

    root->addWidget(splitter, 1);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

QString CallFrame::selectedCallId() const
{
    auto items = callTree_->selectedItems();
    if (items.isEmpty())
        return QString();
    return items.first()->data(0, Qt::UserRole).toString();
}

CallRow *CallFrame::findRow(const QString &callId)
{
    for (auto &r : calls_)
        if (r.id == callId)
            return &r;
    return nullptr;
}

QTreeWidgetItem *CallFrame::itemForCall(const QString &callId) const
{
    for (int i = 0; i < callTree_->topLevelItemCount(); ++i) {
        auto *it = callTree_->topLevelItem(i);
        if (it->data(0, Qt::UserRole).toString() == callId)
            return it;
    }
    return nullptr;
}

void CallFrame::refreshTreeItem(QTreeWidgetItem *item, const CallRow &row)
{
    item->setText(0, row.peer.isEmpty() ? row.id : row.peer);
    item->setText(1, stateName(row.state));
    item->setText(2, mediaSummary(row));
    item->setData(0, Qt::UserRole, row.id);
}

CallRow *CallFrame::upsertRow(const CallRow &row)
{
    if (CallRow *existing = findRow(row.id)) {
        *existing = row;
        if (auto *it = itemForCall(row.id))
            refreshTreeItem(it, *existing);
        return existing;
    }
    calls_.append(row);
    CallRow *added = &calls_.last();
    auto *it = new QTreeWidgetItem(callTree_);
    refreshTreeItem(it, *added);
    return added;
}

void CallFrame::appendStatus(const QString &html)
{
    statusView_->append(html);
}

void CallFrame::updateControls()
{
    const QString id = selectedCallId();
    CallRow *row = id.isEmpty() ? nullptr : findRow(id);

    const bool ringingIn = row && row->state == ST_RINGING_IN;
    const bool active    = row && row->state == ST_ACTIVE;

    actAnswer_->setEnabled(ringingIn);
    actReject_->setEnabled(ringingIn);
    actHangup_->setEnabled(row && row->state != ST_ENDED);

    controls_->setEnabled(active);
    btnHangup_->setEnabled(active);
    btnMic_->setEnabled(active);
    btnCamera_->setEnabled(active && row->hasVideo);
    btnScreen_->setEnabled(active);

    if (row) {
        // reflect local mute state without re-emitting
        QSignalBlocker b1(btnMic_), b2(btnCamera_), b3(btnScreen_);
        btnMic_->setChecked(row->localAudioMuted);
        btnCamera_->setChecked(row->localVideoMuted);
        btnScreen_->setChecked(row->localScreenSharing);

        QString hdr = tr("Call with %1 — %2").arg(row->peer, stateName(row->state));
        header_->setText(hdr);
        videoArea_->setText(row->hasVideo && !row->localVideoMuted
                            ? tr("Video (preview unavailable)")
                            : tr("No video"));
    } else {
        header_->setText(tr("No call selected"));
        videoArea_->setText(tr("No video"));
    }
}

void CallFrame::sendMediaControl(const CallRow &row)
{
    emit mediaControlRequested(row.id, row.localAudioMuted, row.localVideoMuted,
                               row.localScreenSharing);
}

// ---------------------------------------------------------------------------
// State pushed by HubFrame
// ---------------------------------------------------------------------------

void CallFrame::setCallList(const QList<CallRow> &calls)
{
    const QString sel = selectedCallId();
    calls_ = calls;
    callTree_->clear();
    for (const auto &row : calls_) {
        auto *it = new QTreeWidgetItem(callTree_);
        refreshTreeItem(it, row);
        if (row.id == sel)
            it->setSelected(true);
    }
    updateControls();
}

void CallFrame::callOffer(const QString &callId, const QString &fromNick, bool isGroup,
                          const QList<int> &media)
{
    CallRow row;
    row.id = callId;
    row.peer = fromNick;
    row.isGroup = isGroup;
    row.state = ST_RINGING_IN;
    row.hasVideo = media.contains(MEDIA_VIDEO);
    upsertRow(row);
    appendStatus(tr("<b>Incoming call</b> from %1 (%2)")
                     .arg(escapeHtml(fromNick), mediaSummary(row)));
    updateControls();
}

void CallFrame::callAnswer(const QString &callId, const QString &fromNick, bool accepted,
                           const QString &rejectReason)
{
    if (CallRow *row = findRow(callId)) {
        row->state = accepted ? ST_ACTIVE : ST_ENDED;
        if (auto *it = itemForCall(callId))
            refreshTreeItem(it, *row);
    }
    if (accepted)
        appendStatus(tr("%1 accepted the call").arg(escapeHtml(fromNick)));
    else
        appendStatus(rejectReason.isEmpty()
                         ? tr("%1 rejected the call").arg(escapeHtml(fromNick))
                         : tr("%1 rejected the call: %2")
                               .arg(escapeHtml(fromNick), escapeHtml(rejectReason)));
    updateControls();
}

void CallFrame::callEnded(const QString &callId, int reason, quint32 durationSec)
{
    if (CallRow *row = findRow(callId)) {
        row->state = ST_ENDED;
        if (auto *it = itemForCall(callId))
            refreshTreeItem(it, *row);
    }
    appendStatus(tr("Call ended (reason %1, %2s)").arg(reason).arg(durationSec));
    updateControls();
}

void CallFrame::callMediaControl(const QString &callId, const QString &fromNick,
                                 bool audioMuted, bool videoMuted, bool screenSharing)
{
    if (CallRow *row = findRow(callId)) {
        row->remoteAudioMuted = audioMuted;
        row->remoteVideoMuted = videoMuted;
        row->remoteScreenSharing = screenSharing;
    }
    QStringList st;
    st << (audioMuted ? tr("mic off") : tr("mic on"));
    if (videoMuted) st << tr("camera off");
    if (screenSharing) st << tr("sharing screen");
    appendStatus(tr("%1: %2").arg(escapeHtml(fromNick), st.join(", ")));
}

void CallFrame::callStatus(const QString &callId, const QString &text)
{
    Q_UNUSED(callId);
    appendStatus(QString("<span style='color:gray'>*** %1</span>").arg(escapeHtml(text)));
}

// ---------------------------------------------------------------------------
// User actions
// ---------------------------------------------------------------------------

void CallFrame::slotCallSelected()
{
    updateControls();
}

void CallFrame::slotNewCall()
{
    bool ok = false;
    const QString nick = QInputDialog::getText(this, tr("New call"),
                                               tr("Call which user?"), QLineEdit::Normal,
                                               QString(), &ok);
    if (!ok || nick.trimmed().isEmpty())
        return;
    const auto withVideo = QMessageBox::question(this, tr("New call"),
                                                 tr("Include video?"),
                                                 QMessageBox::Yes | QMessageBox::No);
    QList<int> media;
    media << MEDIA_AUDIO;
    if (withVideo == QMessageBox::Yes)
        media << MEDIA_VIDEO;
    emit callRequested(nick.trimmed(), media, /*isGroup=*/false, QString());
}

void CallFrame::slotAnswer()
{
    const QString id = selectedCallId();
    if (!id.isEmpty())
        emit answerRequested(id, /*accepted=*/true, QString());
}

void CallFrame::slotReject()
{
    const QString id = selectedCallId();
    if (!id.isEmpty())
        emit answerRequested(id, /*accepted=*/false, tr("declined"));
}

void CallFrame::slotHangup()
{
    const QString id = selectedCallId();
    if (!id.isEmpty())
        emit endRequested(id);
}

void CallFrame::slotToggleMic(bool muted)
{
    CallRow *row = findRow(selectedCallId());
    if (!row)
        return;
    row->localAudioMuted = muted;
    sendMediaControl(*row);
}

void CallFrame::slotToggleCamera(bool off)
{
    CallRow *row = findRow(selectedCallId());
    if (!row)
        return;
    row->localVideoMuted = off;
    videoArea_->setText(row->hasVideo && !off ? tr("Video (preview unavailable)")
                                              : tr("No video"));
    sendMediaControl(*row);
}

void CallFrame::slotToggleScreenShare(bool on)
{
    CallRow *row = findRow(selectedCallId());
    if (!row)
        return;
    row->localScreenSharing = on;
    sendMediaControl(*row);
}

void CallFrame::closeEvent(QCloseEvent *e)
{
    e->accept();
}
