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

#include <QObject>
#include <QJSEngine>
#include <QJSValue>
#include <QFileSystemWatcher>
#include <QMetaType>

#include <QList>

#include <QStringList>
#include <QMap>

#include "dcpp/stdinc.h"
#include "dcpp/DCPlusPlus.h"
#include "dcpp/Singleton.h"

#include "ScriptBridge.h"

struct ScriptObject {
    QJSEngine engine;
    ScriptBridge *bridge = nullptr;
    QString path;
};

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
    void prepareThis(QJSEngine &);

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

    void registerStaticMembers(QJSEngine &);
    void registerDynamicMembers(QJSEngine &);

    QMap<QString, ScriptObject*> scripts;

    QFileSystemWatcher watcher;
    QStringList changedFiles;
};

Q_DECLARE_METATYPE(ScriptEngine*)
#ifndef USE_QML
Q_DECLARE_METATYPE(QList<QObject*>)
#endif
