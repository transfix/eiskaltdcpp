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
#include <QString>
#include <QList>
#include <QMetaType>

#include "ArenaWidget.h"

class QTreeWidget;
class QTreeWidgetItem;
class QLabel;
class QToolButton;
class QAction;
class QGroupBox;
class QTextBrowser;
class QCloseEvent;
/// Qt-friendly view of a call, used for cross-thread state pushes so no
/// dcpp/std types cross a queued connection. Media kinds map to the proto
/// CallMediaType (0=audio, 1=video, 2=screen-share).
struct CallRow {
    QString id;
    QString peer;
    bool isGroup = false;
    QString groupId;
    int state = 0;          ///< CallState: 0=RINGING_OUT 1=RINGING_IN 2=ACTIVE 3=ENDED
    bool hasVideo = false;  ///< negotiated video media
    bool localAudioMuted = false;
    bool localVideoMuted = false;
    bool localScreenSharing = false;
    bool remoteAudioMuted = false;
    bool remoteVideoMuted = false;
    bool remoteScreenSharing = false;
};
Q_DECLARE_METATYPE(CallRow)
Q_DECLARE_METATYPE(QList<CallRow>)

/// Arena widget that presents the NMDCpb voice/video calls of a single hub:
/// the list of active/ringing calls plus in-call controls (mute mic, toggle
/// camera, screen share, hang up) for the selected call, and an incoming-call
/// accept/reject affordance. Like ChannelFrame it is UI-only: it emits
/// *Requested signals for user actions and exposes public slots the owning
/// HubFrame calls (GUI thread) to push signaling state in.
class CallFrame : public QWidget, public ArenaWidget
{
    Q_OBJECT
    Q_INTERFACES(ArenaWidget)

public:
    // CallState mirror (values MUST match dcpp::CallState order).
    enum { ST_RINGING_OUT = 0, ST_RINGING_IN = 1, ST_ACTIVE = 2, ST_ENDED = 3 };
    // CallMediaType mirror (values MUST match dcpp::CallMediaType / proto).
    enum { MEDIA_AUDIO = 0, MEDIA_VIDEO = 1, MEDIA_SCREEN = 2 };

    explicit CallFrame(const QString &hubUrl, const QString &hubName);
    ~CallFrame() override;

    CallFrame(const CallFrame&) = delete;
    CallFrame& operator=(const CallFrame&) = delete;

    // ArenaWidget interface
    QWidget *getWidget() override { return this; }
    QString  getArenaTitle() override;
    QString  getArenaShortTitle() override;
    QMenu   *getMenu() override { return nullptr; }
    ArenaWidget::Role role() const override { return ArenaWidget::Call; }

    QString hubUrl() const { return hubUrl_; }

public Q_SLOTS:
    // ---- State pushed in by HubFrame (GUI thread) ----
    void setCallList(const QList<CallRow> &calls);
    void callOffer(const QString &callId, const QString &fromNick, bool isGroup,
                   const QList<int> &media);
    void callAnswer(const QString &callId, const QString &fromNick, bool accepted,
                    const QString &rejectReason);
    void callEnded(const QString &callId, int reason, quint32 durationSec);
    void callMediaControl(const QString &callId, const QString &fromNick,
                          bool audioMuted, bool videoMuted, bool screenSharing);
    void callStatus(const QString &callId, const QString &text);

Q_SIGNALS:
    // ---- User actions (handled by HubFrame) ----
    void callRequested(const QString &targetNick, const QList<int> &media,
                       bool isGroup, const QString &groupId);
    void answerRequested(const QString &callId, bool accepted, const QString &rejectReason);
    void endRequested(const QString &callId);
    void mediaControlRequested(const QString &callId, bool audioMuted,
                               bool videoMuted, bool screenSharing);

protected:
    void closeEvent(QCloseEvent *e) override;

private Q_SLOTS:
    void slotCallSelected();
    void slotNewCall();
    void slotAnswer();
    void slotReject();
    void slotHangup();
    void slotToggleMic(bool muted);
    void slotToggleCamera(bool off);
    void slotToggleScreenShare(bool on);

private:
    void buildUi();
    void appendStatus(const QString &html);
    void refreshTreeItem(QTreeWidgetItem *item, const CallRow &row);
    void updateControls();
    void sendMediaControl(const CallRow &row);
    QString selectedCallId() const;
    CallRow *findRow(const QString &callId);
    QTreeWidgetItem *itemForCall(const QString &callId) const;
    CallRow *upsertRow(const CallRow &row);
    static QString stateName(int state);
    static QString mediaSummary(const CallRow &row);

    QString hubUrl_;
    QString hubName_;

    QList<CallRow> calls_;

    // widgets
    QTreeWidget *callTree_   = nullptr;
    QLabel      *header_     = nullptr;
    QGroupBox   *controls_   = nullptr;
    QLabel      *videoArea_  = nullptr;
    QTextBrowser *statusView_ = nullptr;

    QAction *actNew_    = nullptr;
    QAction *actAnswer_ = nullptr;
    QAction *actReject_ = nullptr;
    QAction *actHangup_ = nullptr;

    QToolButton *btnMic_    = nullptr;
    QToolButton *btnCamera_ = nullptr;
    QToolButton *btnScreen_ = nullptr;
    QToolButton *btnHangup_ = nullptr;
};

Q_DECLARE_METATYPE(CallFrame*)
