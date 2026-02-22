/***************************************************************************
*                                                                         *
*   This program is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License as published by  *
*   the Free Software Foundation; either version 3 of the License, or     *
*   (at your option) any later version.                                   *
*                                                                         *
***************************************************************************/

#include "MultiLineToolBar.h"
#include "WulforSettings.h"
#include "ArenaWidgetManager.h"

#include <QMenu>
#include <QWheelEvent>

MultiLineToolBar::MultiLineToolBar(QWidget *parent) :
    QToolBar(parent)
{
    setObjectName("multiLineTabbar");

    frame = new TabFrame();

    addWidget(frame);

    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    setContextMenuPolicy(Qt::CustomContextMenu);

    connect(ArenaWidgetManager::getInstance(), &ArenaWidgetManager::added,     frame, &TabFrame::insertWidget);
    connect(ArenaWidgetManager::getInstance(), &ArenaWidgetManager::removed,   frame, &TabFrame::removeWidget);
    connect(ArenaWidgetManager::getInstance(), &ArenaWidgetManager::updated,   frame, &TabFrame::updated);
    connect(ArenaWidgetManager::getInstance(), &ArenaWidgetManager::activated, frame, &TabFrame::mapped);
    connect(ArenaWidgetManager::getInstance(), &ArenaWidgetManager::toggled,   frame, &TabFrame::toggled);
    connect(this, &MultiLineToolBar::nextTab, frame, &TabFrame::nextTab);
    connect(this, &MultiLineToolBar::prevTab, frame, &TabFrame::prevTab);
    connect(this, &MultiLineToolBar::moveTabLeft, frame, &TabFrame::moveLeft);
    connect(this, &MultiLineToolBar::moveTabRight, frame, &TabFrame::moveRight);
    connect(this, &QWidget::customContextMenuRequested, this, &MultiLineToolBar::slotContextMenu);
}

MultiLineToolBar::~MultiLineToolBar(){
    frame->deleteLater();
}

void MultiLineToolBar::wheelEvent(QWheelEvent *e){
    e->ignore();

    if (e->angleDelta().y() > 0)
        emit nextTab();
    else if (e->angleDelta().y() < 0)
        emit prevTab();
}

void MultiLineToolBar::slotContextMenu(){
    QMenu *m = new QMenu(this);
    QAction *act = new QAction(tr("Show close buttons"), m);

    act->setCheckable(true);
    act->setChecked(WBGET(WB_APP_TBAR_SHOW_CL_BTNS));

    m->addAction(act);

    if (m->exec(QCursor::pos())){
        WBSET(WB_APP_TBAR_SHOW_CL_BTNS, act->isChecked());
    }

    m->deleteLater();
}
