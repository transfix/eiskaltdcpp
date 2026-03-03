/*
 * Stubs for WulforUtil methods referenced by WulforSettings.cpp and
 * Antispam.cpp but not needed in Qt-only tests.  These satisfy the linker
 * without pulling in the full WulforUtil.cpp (which drags in MainWindow,
 * ClientManager, icon loading, etc.).
 */

#include <QString>
#include "WulforUtil.h"

WulforUtil* WulforUtil::getInstance() {
    return nullptr;  // Tests don't create WulforUtil
}

QString WulforUtil::getTranslationsPath() const {
    return QString();
}

QString WulforUtil::getNicks(const QString & /*cid*/, const QString & /*hintUrl*/) {
    return QString();
}

QString WulforUtil::getNicks(const dcpp::CID & /*cid*/, const QString & /*hintUrl*/) {
    return QString();
}
