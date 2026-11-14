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
#include <QEvent>
#include <QKeyEvent>
#include <QShowEvent>
#include <QHideEvent>
#include <QMap>
#include <QMenu>
#include <QAction>
#include <QHash>
#include <QSortFilterProxyModel>
#include <QCompleter>
#include <QMetaType>
#include <QTextBlockUserData>
#include <memory>

#include "ui_HubFrame.h"

#include "dcpp/stdinc.h"
#include "dcpp/FastAlloc.h"
#include "dcpp/ClientListener.h"
#include "dcpp/ClientManager.h"
#include "dcpp/ClientManagerListener.h"
#include "dcpp/User.h"
#include "dcpp/Client.h"
#include "dcpp/FavoriteManagerListener.h"

#include "ArenaWidget.h"
#include "WulforUtil.h"

#ifdef WITH_NMDCPB
#include "dcpp/ChannelManager.h"
#include "dcpp/CallManager.h"
#include "ChannelFrame.h"
#include "CallFrame.h"
namespace dcpp { class NmdcHub; }
#endif

class ShellCommandRunner;
class PMWindow;
class HubFramePrivate;

using namespace dcpp;

class UserListUserData : public QTextBlockUserData
{
public:
    UserListUserData(const QString& nick) : data(nick) { }
    QString data;
};

class HubFrame :
        public  QWidget,
        private Ui::UIHubFrame,
        private dcpp::ClientListener,
        private FavoriteManagerListener,
#ifdef WITH_NMDCPB
        private dcpp::ChannelManagerListener,
        private dcpp::CallManagerListener,
#endif
        public  ArenaWidget
{
    Q_OBJECT
    Q_INTERFACES(ArenaWidget)

    class Menu{

    public:

        enum Action{
            /** Actions for userlist */
            CopyText=0,
            SearchText,
            CopyNick,
            FindInList,
            BrowseFilelist,
            MatchQueue,
            PrivateMessage,
            FavoriteAdd,
            FavoriteRem,
            GrantSlot,
            RemoveQueue,
            UserCommands,

            /** Additional actions for chat */
            ClearChat,
            FindInChat,
            DisableChat,
            SelectAllChat,
            ZoomInChat,
            ZoomOutChat,

            None,

            /** Additional actions for userlist */
            CopyComment,
            CopyIP,
            CopyShare,
            CopyTag,
            CopyEmail,

            /** Additional actions for AntiSpam */
            AntiSpamWhite,
            AntiSpamBlack
        };

        Menu();
        virtual ~Menu();

        Menu(const Menu&) = delete;
        Menu& operator=(const Menu&) = delete;

        Action execUserMenu(Client*, const QString&);
        Action execChatMenu(Client*, const QString&, bool pmw);

        QString getLastUserCmd() const;

    private:
        QMenu *menu;
        QList<QAction*> actions;           // actions list for menu
        QList<QAction*> pm_actions;        // actions list for menu in PMWindow
        QList<QAction*> ul_actions;        // actions list for menu in user list
        QList<QAction*> chat_actions;      // chat actions list for menu
        QList<QAction*> pm_chat_actions;   // chat actions list for menu in PMWindow
        QMap<QAction*, Action> chat_actions_map; //chat menu has separators and because of it all actions are mapped
        QString last_user_cmd;
    };

public:
    class LinkParser{
    public:
       static QString parseForLinks(QString, bool);
       static void parseForMagnetAlias(QString &output);//find and replace <magnet ...></magnet> sections
    };

    typedef QMap<QString, PMWindow*> PMMap;
    typedef QVariantMap VarMap;
    typedef QList<ShellCommandRunner*> ShellList;

    HubFrame(QWidget *parent, QString, QString);
    ~HubFrame();

    HubFrame(const HubFrame&) = delete;
    HubFrame& operator=(const HubFrame&) = delete;

    bool parseForCmd(QString, QWidget *);

    void createPMWindow(const QString&);
    void createPMWindow(const dcpp::CID&);

    bool hasCID(const dcpp::CID &, const QString &);

    inline void reconnect() { slotReconnect(); }

    // PM functions
    void addPM(QString, QString, bool keepfocus = true, QString nick = QString());

    // Arena Widget interface
    QWidget *getWidget();
    QString getArenaTitle();
    QString getArenaShortTitle();
    QMenu *getMenu();
    const QPixmap &getPixmap();
    void requestClear() { clearChat(); }
    void requestFilter() { slotShowSearchBar(); }
    void requestFocus() { plainTextEdit_INPUT->setFocus(); }
    ArenaWidget::Role role() const { return ArenaWidget::Hub; }

    QString getCIDforNick(QString nick);

Q_SIGNALS:
    void coreConnecting(QString);
    void coreConnected(QString);
    void coreUserUpdated(const dcpp::UserPtr &user, const dcpp::Identity &id);
    void coreUserRemoved(const dcpp::UserPtr &user, const dcpp::Identity &id);
    void coreStatusMsg(QString);
    void coreFollow(QString);
    void coreFailed();
    void corePassword();
    void coreMessage(const VarMap&);
    void corePrivateMsg(const VarMap&);
    void coreHubUpdated();
    void coreFavoriteUserAdded(QString);
    void coreFavoriteUserRemoved(QString);
    void closeRequest();
    void highlighted(const VarMap&);
    void new_msg(const VarMap&);
    void coreE2EPMStatus(QString nick, QString fingerprint, bool keyChanged);

#ifdef WITH_NMDCPB
    void coreChannelList(const QList<ChannelRow> &channels);
    void coreChannelCreated(QString channelId, QString ownerNick, bool isPrivate);
    void coreChannelMemberUpdate(QString channelId, QString nick, int updateType,
                                 int newRole, QString reason);
    void coreChannelMessage(QString channelId, QString fromNick, QString text,
                            bool isAction, quint64 timestamp);
    void coreChannelInvite(QString channelId, QString inviterNick);

    void coreCallOffer(QString callId, QString fromNick, bool isGroup, QList<int> media);
    void coreCallAnswer(QString callId, QString fromNick, bool accepted, QString rejectReason);
    void coreCallEnd(QString callId, int reason, quint32 durationSec);
    void coreCallMediaControl(QString callId, QString fromNick, bool audioMuted,
                              bool videoMuted, bool screenSharing);
#endif

public Q_SLOTS:
    void disableChat();
    void clearChat();
    void addStatus(QString);
    QString getHubUrl();
    QString getHubName();
    QString getMyNick();
    void sendMsg(const QString&);
    void disablePrivateMessages(bool disable);//disconnect corePrivateMsg from this

    void reloadSomeSettings();
    void slotShowSearchBar();
    void slotHideSearchBar();
    void slotActivate();
    void nextMsg();
    void prevMsg();

    void getStatistic(quint64 &users, quint64 &share) const;
    bool isConnected() const;
    bool isOP(const QString &nick);

    void browseUserFiles(const QString&, bool=false);

protected:
    virtual bool eventFilter(QObject *, QEvent *);
    virtual void closeEvent(QCloseEvent*);
    virtual void showEvent(QShowEvent *);
    virtual void hideEvent(QHideEvent *);

    void sendChat(QString, bool, bool);
    void save();
    void load();

private Q_SLOTS:
    void slotUsersUpdated();
    void slotReconnect();
    void slotMapOnArena();
    void slotClose();
    void slotPMClosed(QString);
    void slotUserListMenu(const QPoint&);
    void slotChatMenu(const QPoint&);
    void slotHeaderMenu(const QPoint&);
    void slotShowWnd();
    void slotShellFinished(bool, QString);
    void slotFilterTextChanged();
    void slotFindForward() { findText(QTextDocument::FindFlags()); }
    void slotFindBackward(){ findText(QTextDocument::FindBackward); }
    void slotFindTextEdited(const QString & text);
    void slotInputTextChanged();
    void slotInputContextMenu();
    void slotFindAll();
    void slotStatusLinkOpen(const QString &url);
    void slotHubMenu(QAction*);
    void slotSmile();
    void slotSmileClicked();
    void slotSmileContextMenu();
    void slotSettingsChanged(const QString &, const QString &);
    void slotBoolSettingsChanged(const QString&, int);
    void slotCopyHubURL();
    void slotCopyHubTitle();
    void slotCopyHubIP();

    void grantSlot(const QString&);
    void addUserToFav(const QString&);
    void delUserFromFav(const QString&);
    void changeFavStatus(const QString&);
    void delUserFromQueue(const QString&);
    void addAsFavorite();

    void userUpdated(const dcpp::UserPtr&, const dcpp::Identity&);
    void userRemoved(const dcpp::UserPtr&, const dcpp::Identity&);
    void follow(QString);
    void clearUsers();
    void getPassword();
    void newMsg(const VarMap&);
    void newPm(const VarMap&);
    void updateE2EPMStatus(QString nick, QString fingerprint, bool keyChanged);

#ifdef WITH_NMDCPB
    // Open the channels arena widget for this hub
    void slotOpenChannels();
    // GUI-thread appliers (from queued coreChannel* signals)
    void updateChannelList(const QList<ChannelRow> &channels);
    void updateChannelCreated(QString channelId, QString ownerNick, bool isPrivate);
    void updateChannelMemberUpdate(QString channelId, QString nick, int updateType,
                                   int newRole, QString reason);
    void updateChannelMessage(QString channelId, QString fromNick, QString text,
                              bool isAction, quint64 timestamp);
    void updateChannelInvite(QString channelId, QString inviterNick);
    // Action handlers wired from ChannelFrame's *Requested signals
    void chReqRefresh();
    void chReqCreate(const QString &channelId, const QString &name,
                     const QString &topic, bool isPrivate);
    void chReqJoin(const QString &channelId);
    void chReqLeave(const QString &channelId);
    void chReqSend(const QString &channelId, const QString &text, bool isAction);
    void chReqSetTopic(const QString &channelId, const QString &topic);
    void chReqInvite(const QString &channelId, const QString &nick);
    void chReqKick(const QString &channelId, const QString &nick);
    void chReqSetRole(const QString &channelId, const QString &nick, int role);
    void chReqInviteResponse(const QString &channelId, bool accepted);
    void slotChannelFrameDestroyed();

    // Open the calls arena widget for this hub
    void slotOpenCalls();
    // GUI-thread appliers (from queued coreCall* signals)
    void updateCallOffer(QString callId, QString fromNick, bool isGroup, QList<int> media);
    void updateCallAnswer(QString callId, QString fromNick, bool accepted, QString rejectReason);
    void updateCallEnd(QString callId, int reason, quint32 durationSec);
    void updateCallMediaControl(QString callId, QString fromNick, bool audioMuted,
                                bool videoMuted, bool screenSharing);
    // Action handlers wired from CallFrame's *Requested signals
    void clReqOffer(const QString &targetNick, const QList<int> &media,
                    bool isGroup, const QString &groupId);
    void clReqAnswer(const QString &callId, bool accepted, const QString &rejectReason);
    void clReqEnd(const QString &callId);
    void clReqMediaControl(const QString &callId, bool audioMuted,
                           bool videoMuted, bool screenSharing);
    void slotCallFrameDestroyed();
#endif

private:
    // Chat functions
    void addOutput(QString);
    void addUserData(const QString&);

    // GUI setup functions
    void init();
    void initMenu();

    QString getUserInfo(UserListItem *item);

    void pmUserOffline(const QString &);
    void pmUserEvent(const QString &, const QString &);

    void findText(QTextDocument::FindFlags );

    void updateStyles();

    /** Extracts data from user identity */
    void getParams(VarMap &, const Identity &);

    // FavoriteManagerListener
    virtual void on(FavoriteManagerListener::UserAdded, const FavoriteUser& /*aUser*/) noexcept;
    virtual void on(FavoriteManagerListener::UserRemoved, const FavoriteUser& /*aUser*/) noexcept;

    // ClientListener interface
    virtual void on(ClientListener::Connecting, Client*) noexcept;
    virtual void on(ClientListener::Connected, Client*) noexcept;
    virtual void on(ClientListener::UserUpdated, Client*, const OnlineUser&) noexcept;
    virtual void on(ClientListener::UsersUpdated, Client*, const OnlineUserList&) noexcept;
    virtual void on(ClientListener::UserRemoved, Client*, const OnlineUser&) noexcept;
    virtual void on(ClientListener::Redirect, Client*, const string&) noexcept;
    virtual void on(ClientListener::Failed, Client*, const string&) noexcept;
    virtual void on(ClientListener::GetPassword, Client*) noexcept;
    virtual void on(ClientListener::HubUpdated, Client*) noexcept;
    virtual void on(ClientListener::Message, Client*, const ChatMessage&) noexcept;
    virtual void on(ClientListener::StatusMessage, Client*, const string&, int = ClientListener::FLAG_NORMAL) noexcept;
    virtual void on(ClientListener::NickTaken, Client*) noexcept;
    virtual void on(ClientListener::SearchFlood, Client*, const string&) noexcept;
    virtual void on(ClientListener::E2EPMStatus, Client*, const string&, const string&, bool) noexcept;

#ifdef WITH_NMDCPB
    // ChannelManagerListener interface (fires on core threads → emit queued signals)
    void onChannelList(const std::vector<dcpp::ChannelInfo>& channels) override;
    void onChannelCreated(const std::string& channelId, const std::string& ownerNick,
                          bool isPrivate) override;
    void onChannelMemberUpdate(const std::string& channelId, const std::string& nick,
                               int updateType, dcpp::ChannelRole newRole,
                               const std::string& reason) override;
    void onChannelMessage(const std::string& channelId, const std::string& fromNick,
                          const std::string& text, bool isAction,
                          uint64_t timestamp) override;
    void onChannelInvite(const std::string& channelId, const std::string& targetNick,
                         const std::string& inviterNick) override;

    // CallManagerListener interface (fires on core threads → emit queued signals)
    void onCallOffer(const std::string& callId, const std::string& fromNick,
                     bool isGroup, const std::vector<dcpp::CallMediaType>& media) override;
    void onCallAnswer(const std::string& callId, const std::string& fromNick,
                      bool accepted, const std::string& rejectReason) override;
    void onCallEnd(const std::string& callId, dcpp::CallEndReason reason,
                   uint32_t durationSec) override;
    void onCallMediaControl(const std::string& callId, const std::string& fromNick,
                            bool audioMuted, bool videoMuted, bool screenSharing) override;

    // NMDCpb channel helpers
    void setupNmdcpbChannels();      ///< called from init(): dynamic_cast + addListener
    void teardownNmdcpbChannels();   ///< called from closeEvent: removeListener
    void wireChannelFrame();         ///< connect ChannelFrame signals ↔ HubFrame slots
    QList<ChannelRow> collectChannelRows();

    // NMDCpb call helpers
    void setupNmdcpbCalls();         ///< called from init(): dynamic_cast + addListener
    void teardownNmdcpbCalls();      ///< called from closeEvent: removeListener
    void wireCallFrame();            ///< connect CallFrame signals ↔ HubFrame slots
    QList<CallRow> collectCallRows();

    dcpp::NmdcHub *nmdcHub_ = nullptr;
    ChannelFrame  *channelFrame_ = nullptr;
    CallFrame     *callFrame_ = nullptr;
#endif

    Q_DECLARE_PRIVATE(HubFrame)
    HubFramePrivate *d_ptr;

    std::unique_ptr<Menu> menu_;
};

Q_DECLARE_METATYPE(HubFrame*)
