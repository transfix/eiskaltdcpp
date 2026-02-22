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
#include <QAction>
#include <QIcon>
#include <QMap>
#if QT_VERSION >= 0x060000
#include <QJSEngine>
#else
#include <QtScript/QScriptEngine>
#endif

class MainWindowScript : public QObject
{
Q_OBJECT
public:
#if QT_VERSION >= 0x060000
    explicit MainWindowScript(QJSEngine *engine, QObject *parent = nullptr);
#else
    explicit MainWindowScript(QScriptEngine *engine, QObject *parent = nullptr);
#endif
    virtual ~MainWindowScript();

public slots:
    bool addToolButton(const QString &name, const QString &title, const QIcon &icon);
    bool remToolButton(const QString &name);
    bool addMenu(QMenu *menu);
    bool remMenu(QMenu *menu);

private:
#if QT_VERSION >= 0x060000
    QJSEngine *engine;
#else
    QScriptEngine *engine;
#endif
    QMap<QString, QAction*> actions;
    QMap<QMenu*, QAction*> menus;
};
