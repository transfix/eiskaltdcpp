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
#include <QtXml>
#include <QImage>
#include <QTextDocument>
#include <QLayout>
#include <QSize>

#include "EmoticonObject.h"

#include "dcpp/stdinc.h"

class QtContext;

class EmoticonFactory :
        public QObject
{
Q_OBJECT
typedef QList<QDomNode> DomNodeList;
typedef QList<QTextDocument*> TextDocumentList;

friend class QtContext;

public:
    EmoticonFactory();
    ~EmoticonFactory() override;

    static EmoticonFactory* getInstance();

    void load();
    void unload(){ this->clear(); }

    void addEmoticons(QTextDocument *to);
    QString convertEmoticons(const QString &html);
    void fillLayout(QLayout *l, QSize &recommendedSize);
    const EmoticonMap &getEmoticons() { return map; }

private slots:
    void slotDocDeleted();

private:
    void clear();
    void createEmoticonMap(const QDomNode &root);

    QDomNode findSectionByName(const QDomNode &node, const QString &name);
    void getSubSectionsByName(const QDomNode &node, DomNodeList &list, const QString &name);

    QString currentTheme;

    EmoticonMap map;
    EmoticonList list;
    TextDocumentList docs;
};
