/***************************************************************************
*                                                                         *
*   This program is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License as published by  *
*   the Free Software Foundation; either version 3 of the License, or     *
*   (at your option) any later version.                                   *
*                                                                         *
***************************************************************************/

#pragma once

#include <QObject>
// Use hex literal instead of QT_VERSION_CHECK() — Qt5 moc doesn't expand it
#if QT_VERSION >= 0x060000
#include <QJSEngine>
#include <QJSValue>
#else
#include <QtScript/QScriptEngine>
#include <QtScript/QScriptValue>
#endif
#include <QFileSystemWatcher>
#include <QMetaType>

#include <QList>

#include <QStringList>
#include <QMap>

#include "dcpp/stdinc.h"
#include "dcpp/DCPlusPlus.h"
#include "dcpp/Singleton.h"

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include "ScriptBridge.h"

struct ScriptObject {
    QJSEngine engine;
    ScriptBridge *bridge = nullptr;
    QString path;
};

#else // Qt5

struct ScriptObject {
    QScriptEngine engine;
    QString path;
};

#endif

class ScriptEngine :
        public QObject,
        public dcpp::Singleton<ScriptEngine>
{
Q_OBJECT
friend class dcpp::Singleton<ScriptEngine>;

public:

Q_SIGNALS:
    void scriptChanged(const QString &script);

public Q_SLOTS:
    void loadScripts();
    void loadScript(const QString&);
    void stopScripts();
    void stopScript(const QString&);

public:
    // Not Q_SLOTS — called directly, and conditional types confuse the moc preprocessor
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    void prepareThis(QJSEngine &);
#else
    void prepareThis(QScriptEngine &);
#endif

private Q_SLOTS:
    void slotWSKeyChanged(const QString &key, const QString &value);
    void slotScriptChanged(const QString &script);
    void slotProcessChangedFiles();

private:
    ScriptEngine();
    virtual ~ScriptEngine();

    void loadJSScript(const QString&);
#ifdef USE_QML
    void loadQMLScript(const QString&);
#endif

    ScriptEngine(const ScriptEngine&) {}
    ScriptEngine &operator =(const ScriptEngine&){ return *this; }

    // Conditional types confuse the moc preprocessor — keep in private (non-slot) section
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    void registerStaticMembers(QJSEngine &);
    void registerDynamicMembers(QJSEngine &);
#else
    void registerStaticMembers(QScriptEngine &);
    void registerDynamicMembers(QScriptEngine &);
#endif

    QMap<QString, ScriptObject*> scripts;

    QFileSystemWatcher watcher;
    QStringList changedFiles;
};

Q_DECLARE_METATYPE(ScriptEngine*)
#ifndef USE_QML
Q_DECLARE_METATYPE(QList<QObject*>)
#endif
