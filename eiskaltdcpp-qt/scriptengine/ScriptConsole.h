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

#include <QtGlobal>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QJSEngine>
#include <QJSValue>
#include "ConsolePrinter.h"
#else
#include <QtScript/QScriptEngine>
#include <QtScript/QScriptValue>
#endif
#include <QDialog>

#include "ui_UIDialogScriptConsole.h"

class ScriptConsole : public QDialog,
                      private Ui::DialogScriptConsole
{
Q_OBJECT
public:
    explicit ScriptConsole(QWidget *parent = nullptr);

private Q_SLOTS:
    void startEvaluation();
    void stopEvaluation();

private:
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QJSEngine engine;
    ConsolePrinter *printer = nullptr;
#else
    QScriptEngine engine;
#endif
};
