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
#include "dcpp/LogManager.h"
#include "dcpp/LogManagerListener.h"

class QtContext;

class LogManagerScript :
        public QObject,
        public dcpp::LogManagerListener
{
Q_OBJECT
friend class QtContext;

Q_SIGNALS:
    void message(const QString &tstamp, const QString &msg);

protected:
    virtual void on(Message, time_t, const dcpp::string&) throw();

public:
    static LogManagerScript* getInstance();
    LogManagerScript(QObject *parent = nullptr);
    ~LogManagerScript();

private:
    LogManagerScript(const LogManagerScript&){}
    LogManagerScript &operator=(const LogManagerScript&){ return *this; }
};
