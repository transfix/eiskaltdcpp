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
#include <QList>
#include <QRegularExpression>
#include <QString>

#include "dcpp/stdinc.h"
#include "dcpp/NonCopyable.h"

class SearchBlacklist:
        public QObject
{
    Q_OBJECT

public:
    enum Argument {
        NAME,
        TTH
    };

    static SearchBlacklist* getInstance()  { return instance_; }
    static void newInstance()    { delete instance_; instance_ = new SearchBlacklist(); }
    static void deleteInstance() { delete instance_; instance_ = nullptr; }

    bool ok(const QString &exp, Argument type);
    QList<QString> getList(Argument arg) const { return (list[arg]); }
    void setList(Argument arg, const QList<QString> &l) { list[arg] = l; }

private:
    SearchBlacklist();
    virtual ~SearchBlacklist();

    static SearchBlacklist* instance_;

    void loadLists();
    void saveLists();

    QList<QString> list[2];
};
