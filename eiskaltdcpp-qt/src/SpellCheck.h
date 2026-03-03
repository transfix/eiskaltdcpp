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
#include <QList>
#include <QStringList>

#include <aspell.h>

#include "dcpp/stdinc.h"

class QtContext;

class SpellCheck :
        public QObject
{
Q_OBJECT
friend class QtContext;

public:
    SpellCheck(QObject *parent = nullptr);
    ~SpellCheck() override;

    static SpellCheck* getInstance();

    bool ok(const QString &word);
    void suggestions(const QString &word, QStringList &list);
    void setLanguage(const QString &lang);
    void addToDict(const QString &word);

private:
    void deleteSpeller();
    void loadAspellConfig(AspellConfig * const config);
    AspellConfig *defaultAspellConfig();

    AspellSpeller *spell_checker;
};
