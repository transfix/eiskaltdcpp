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

#include "dcpp/stdinc.h"
#include "dcpp/DCPlusPlus.h"
#include "dcpp/HashManager.h"
#include "dcpp/HashManagerListener.h"
#include "dcpp/HashValue.h"

class QtContext;

class HashManagerScript :
        public QObject,
        public dcpp::HashManagerListener
{
Q_OBJECT
friend class QtContext;

public Q_SLOTS:
    void stopHashing(const QString &baseDir);
    QString getTTH(const QString &aFileName, quint64 size) const;
    QString getTTH(const QString &aFileName) const;
    void rebuild();
    void startup();
    void shutdown();
    bool pauseHashing() const;
    void resumeHashing();
    bool isHashingPaused() const;

Q_SIGNALS:
    void done(const QString &file, const QString &tth);

protected:
    virtual void on(TTHDone, const dcpp::string& , const dcpp::TTHValue&) throw();

public:
    static HashManagerScript* getInstance();
    HashManagerScript(QObject *parent = nullptr);
    ~HashManagerScript();

private:
    HashManagerScript(const HashManagerScript&);
    HashManagerScript &operator=(const HashManagerScript&);

    dcpp::HashManager *HM;
};
