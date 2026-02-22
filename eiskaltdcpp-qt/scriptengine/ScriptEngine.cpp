/***************************************************************************
*                                                                         *
*   This program is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License as published by  *
*   the Free Software Foundation; either version 3 of the License, or     *
*   (at your option) any later version.                                   *
*                                                                         *
***************************************************************************/

#include "ScriptEngine.h"
#include "dcpp/DCPlusPlus.h"

#include "ArenaWidget.h"
#include "ArenaWidgetFactory.h"
#include "MainWindow.h"
#include "Antispam.h"
#include "DownloadQueue.h"
#include "FavoriteHubs.h"
#include "FavoriteUsers.h"
#include "Notification.h"
#include "HubFrame.h"
#include "HubManager.h"
#include "SearchFrame.h"
#include "ShellCommandRunner.h"
#include "WulforSettings.h"
#include "DebugHelper.h"
#include "GlobalTimer.h"

#include "scriptengine/ClientManagerScript.h"
#include "scriptengine/HashManagerScript.h"
#include "scriptengine/LogManagerScript.h"
#include "scriptengine/MainWindowScript.h"

#include <QProcess>
#include <QDir>
#include <QFile>
#include <QTextStream>
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <QScriptValueIterator>
#endif

#ifndef CLIENT_SCRIPTS_DIR
#define CLIENT_SCRIPTS_DIR
#endif

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)

// ============================================================
// Qt6: ScriptBridge implementation
// ============================================================

ScriptBridge::ScriptBridge(QJSEngine *engine, QObject *parent)
    : QObject(parent), m_engine(engine) {}

void ScriptBridge::shellExec(const QString &cmd, const QStringList &args) {
    QProcess *process = new QProcess();
    QObject::connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
                     process, &QProcess::deleteLater);
    process->start(cmd, args);
}

QJSValue ScriptBridge::getMagnets(const QStringList &files) {
    QStringList magnets;
    for (const auto &f : files) {
        QFile file(f);
        if (!file.exists())
            continue;
        const dcpp::TTHValue *tth = dcpp::getContext()->getHashManager()->getFileTTHif(_tq(f));
        if (tth)
            magnets.push_back(WulforUtil::getInstance()->makeMagnet(
                f.split(QDir::separator(), Qt::SkipEmptyParts).last(),
                file.size(), _q(tth->toBase32())));
    }
    QJSValue array = m_engine->newArray(magnets.size());
    for (int i = 0; i < magnets.length(); i++)
        array.setProperty(i, QJSValue(magnets.at(i)));
    return array;
}

void ScriptBridge::printErr(const QString &msg) {
    qWarning() << qPrintable(msg);
}

void ScriptBridge::Import(const QString &name) {
    qWarning() << "ScriptBridge> Import() is not supported in Qt6. Ignoring:" << name;
}

QJSValue ScriptBridge::Include(const QString &path) {
    QFile f(path);
    if (!(f.exists() && f.open(QIODevice::ReadOnly)))
        return QJSValue();
    QTextStream stream(&f);
    QString data = stream.readAll();
    QJSValue ret = m_engine->evaluate(data, path);
    if (ret.isError())
        qDebug() << "ScriptBridge> Include error:" << ret.property("stack").toString();
    return ret;
}

QJSValue ScriptBridge::Eval(const QString &code) {
    QJSValue ret = m_engine->evaluate(code);
    if (ret.isError())
        qDebug() << "ScriptBridge> Eval error:" << ret.property("stack").toString();
    return ret;
}

QString ScriptBridge::parseChatLinks(const QString &text) {
    return HubFrame::LinkParser::parseForLinks(text, false);
}

QString ScriptBridge::parseMagnetAlias(const QString &text) {
    QString output = text;
    HubFrame::LinkParser::parseForMagnetAlias(output);
    return output;
}

QJSValue ScriptBridge::getStaticMember(const QString &className) {
    QObject *obj = nullptr;
    if (className == "AntiSpam") {
        if (!AntiSpam::getInstance()) {
            AntiSpam::newInstance();
            AntiSpam::getInstance()->loadSettings();
            AntiSpam::getInstance()->loadLists();
        }
        obj = qobject_cast<QObject*>(AntiSpam::getInstance());
    }
    else if (className == "DownloadQueue")
        obj = qobject_cast<QObject*>(ArenaWidgetFactory().create<dcpp::Singleton, DownloadQueue>());
    else if (className == "FavoriteHubs")
        obj = qobject_cast<QObject*>(ArenaWidgetFactory().create<dcpp::Singleton, FavoriteHubs>());
    else if (className == "FavoriteUsers")
        obj = qobject_cast<QObject*>(ArenaWidgetFactory().create<dcpp::Singleton, FavoriteUsers>());
    else if (className == "Notification") {
        if (Notification::getInstance()) {
            m_engine->globalObject().setProperty("NOTIFY_ANY", (int)Notification::ANY);
            obj = qobject_cast<QObject*>(Notification::getInstance());
        }
    }
    else if (className == "HubManager")
        obj = qobject_cast<QObject*>(HubManager::getInstance());
    else if (className == "ClientManagerScript") {
        if (!ClientManagerScript::getInstance()) {
            ClientManagerScript::newInstance();
            ClientManagerScript::getInstance()->moveToThread(MainWindow::getInstance()->thread());
        }
        obj = qobject_cast<QObject*>(ClientManagerScript::getInstance());
    }
    else if (className == "HashManagerScript") {
        if (!HashManagerScript::getInstance()) {
            HashManagerScript::newInstance();
            HashManagerScript::getInstance()->moveToThread(MainWindow::getInstance()->thread());
        }
        obj = qobject_cast<QObject*>(HashManagerScript::getInstance());
    }
    else if (className == "LogManagerScript") {
        if (!LogManagerScript::getInstance()) {
            LogManagerScript::newInstance();
            LogManagerScript::getInstance()->moveToThread(MainWindow::getInstance()->thread());
        }
        obj = qobject_cast<QObject*>(LogManagerScript::getInstance());
    }
    else if (className == "WulforUtil")
        obj = qobject_cast<QObject*>(WulforUtil::getInstance());
    else if (className == "WulforSettings")
        obj = qobject_cast<QObject*>(WulforSettings::getInstance());

    return m_engine->newQObject(obj);
}

QJSValue ScriptBridge::createHubFrame(const QString &url, const QString &enc) {
    HubFrame *fr = ArenaWidgetFactory().create<HubFrame, MainWindow*, QString, QString>(
        MainWindow::getInstance(), url, enc);
    return m_engine->newQObject(qobject_cast<QObject*>(fr));
}

QJSValue ScriptBridge::createSearchFrame() {
    SearchFrame *fr = ArenaWidgetFactory().create<SearchFrame>();
    return m_engine->newQObject(qobject_cast<QObject*>(fr));
}

QJSValue ScriptBridge::createShellCommandRunner(const QString &cmd, const QStringList &args) {
    ShellCommandRunner *runner = new ShellCommandRunner(cmd, args, MainWindow::getInstance());
    QObject::connect(runner, qOverload<bool, const QString &>(&ShellCommandRunner::finished), runner, &QObject::deleteLater);
    return m_engine->newQObject(qobject_cast<QObject*>(runner));
}

QJSValue ScriptBridge::createMainWindowScript() {
    return m_engine->newQObject(qobject_cast<QObject*>(
        new MainWindowScript(m_engine, MainWindow::getInstance())));
}

QJSValue ScriptBridge::createScriptWidget() {
    return m_engine->newQObject(qobject_cast<QObject*>(
        ArenaWidgetFactory().create<ScriptWidget>()));
}

QJSValue ScriptBridge::createIcon(const QString &path) {
    return m_engine->toScriptValue(QIcon(path));
}

#else // Qt5

static QScriptValue shellExec(QScriptContext*, QScriptEngine*);
static QScriptValue getMagnets(QScriptContext*, QScriptEngine*);
static QScriptValue staticMemberConstructor(QScriptContext*, QScriptEngine*);
static QScriptValue dynamicMemberConstructor(QScriptContext*, QScriptEngine*);
static QScriptValue importExtension(QScriptContext*, QScriptEngine*);
static QScriptValue parseChatLinks(QScriptContext*, QScriptEngine*);
static QScriptValue parseMagnetAlias(QScriptContext*, QScriptEngine*);
static QScriptValue eval(QScriptContext*, QScriptEngine*);
static QScriptValue includeFile(QScriptContext*, QScriptEngine*);
static QScriptValue printErr(QScriptContext*, QScriptEngine*);
QScriptValue ScriptVarMapToScriptValue(QScriptEngine* eng, const VarMap& map);
void ScriptVarMapFromScriptValue( const QScriptValue& value, VarMap& map);

#endif // QT_VERSION check

ScriptEngine::ScriptEngine() :
        QObject(nullptr)
{
    DEBUG_BLOCK

    setObjectName("ScriptEngine");

    qRegisterMetaType<ArenaWidget::Flags>("Flags");
    qRegisterMetaType<ArenaWidget::Flags>("ArenaWidget::Flags");

    connect(WulforSettings::getInstance(), &WulforSettings::strValueChanged, this, &ScriptEngine::slotWSKeyChanged);
    connect(&watcher, &QFileSystemWatcher::fileChanged, this, &ScriptEngine::slotScriptChanged);
    connect(GlobalTimer::getInstance(), &GlobalTimer::second, this, &ScriptEngine::slotProcessChangedFiles);

    loadScripts();
}

ScriptEngine::~ScriptEngine(){
    DEBUG_BLOCK

    stopScripts();

    ClientManagerScript::deleteInstance();
    HashManagerScript::deleteInstance();
}

void ScriptEngine::loadScripts(){
    DEBUG_BLOCK

    QStringList enabled = QString(QByteArray::fromBase64(WSGET(WS_APP_ENABLED_SCRIPTS).toLatin1())).split("\n");

    for (const auto &s : enabled)
        loadScript(s);
}

void ScriptEngine::loadScript(const QString &path){
    DEBUG_BLOCK

    QFile f(path);

    if (!f.exists())
        return;

    stopScript(path);

    if (path.endsWith(".js", Qt::CaseInsensitive))
        loadJSScript(path);
#ifdef USE_QML
    else if (path.endsWith(".qml", Qt::CaseInsensitive))
        loadQMLScript(path);
#endif
}

void ScriptEngine::loadJSScript(const QString &file){
    DEBUG_BLOCK

    QFile f(file);

    if (!f.open(QIODevice::ReadOnly))
        return;

    ScriptObject *obj = new ScriptObject;
    obj->path = file;

    QTextStream stream(&f);
    QString data = stream.readAll();

    prepareThis(obj->engine);

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QJSValue scriptPath = QJSValue(file.left(file.lastIndexOf(QDir::separator())) + QDir::separator());
    obj->engine.globalObject().setProperty("SCRIPT_PATH", scriptPath);

    scripts.insert(file, obj);
    watcher.addPath(file);

    QJSValue result = obj->engine.evaluate(data, file);
    if (result.isError()) {
        qDebug() << "ScriptEngine> Error loading" << file << ":"
                 << result.property("stack").toString();
    }
#else
    QScriptValue scriptPath = QScriptValue(&obj->engine, file.left(file.lastIndexOf(QDir::separator())) + QDir::separator());
    obj->engine.globalObject().setProperty("SCRIPT_PATH", scriptPath);

    scripts.insert(file, obj);
    watcher.addPath(file);

    obj->engine.evaluate(data, file);
    if (obj->engine.hasUncaughtException()){
        for (const auto &s : obj->engine.uncaughtExceptionBacktrace())
            qDebug() << s;
    }
#endif
}

#ifdef USE_QML
void ScriptEngine::loadQMLScript(const QString &file){
    DEBUG_BLOCK

    DeclarativeWidget *wgt = ArenaWidgetFactory().create<DeclarativeWidget, QString>(file);
}
#endif

void ScriptEngine::stopScripts(){
    DEBUG_BLOCK

    for (const QString &key : scripts.keys()) {
        stopScript(key);
    }

    scripts.clear();
}

void ScriptEngine::stopScript(const QString &path){
    DEBUG_BLOCK

    if (!scripts.contains(path))
        return;

    watcher.removePath(path);

    ScriptObject *obj = scripts.value(path);

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QJSValue deinit = obj->engine.globalObject().property("deinit");
    if (deinit.isCallable())
        deinit.call();

    obj->engine.setInterrupted(true);

    scripts.remove(path);

    delete obj->bridge;
    delete obj;
#else
    obj->engine.globalObject().property("deinit").call();

    if (obj->engine.isEvaluating())
        obj->engine.abortEvaluation();

    scripts.remove(path);

    if (obj->engine.hasUncaughtException())
        qDebug() << obj->engine.uncaughtExceptionBacktrace();

    delete obj;
#endif
}

void ScriptEngine::slotProcessChangedFiles() {
    DEBUG_BLOCK

    for (const QString &file : changedFiles)
        emit scriptChanged(file);

    changedFiles.clear();
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)

// ============ Qt6 prepareThis using ScriptBridge ============

void ScriptEngine::prepareThis(QJSEngine &engine){
    DEBUG_BLOCK

    ScriptBridge *bridge = new ScriptBridge(&engine);
    QJSValue bridgeVal = engine.newQObject(bridge);
    engine.globalObject().setProperty("_bridge", bridgeVal);

    // Store bridge in the corresponding ScriptObject (if any) for cleanup
    for (auto it = scripts.begin(); it != scripts.end(); ++it) {
        if (&it.value()->engine == &engine) {
            it.value()->bridge = bridge;
            break;
        }
    }

#ifndef _WIN32
    QJSValue scriptsPath = QJSValue(QString(CLIENT_SCRIPTS_DIR) + QDir::separator());
#else
    QJSValue scriptsPath = QJSValue(
        qApp->applicationDirPath() + QDir::separator() + CLIENT_SCRIPTS_DIR + QDir::separator());
#endif
    engine.globalObject().setProperty("SCRIPTS_PATH", scriptsPath);

    // Expose singleton QObjects directly
    engine.globalObject().setProperty("MainWindow",
        engine.newQObject(MainWindow::getInstance()));
    engine.globalObject().setProperty("WulforUtil",
        engine.newQObject(WulforUtil::getInstance()));
    engine.globalObject().setProperty("WulforSettings",
        engine.newQObject(WulforSettings::getInstance()));
    engine.globalObject().setProperty("WidgetManager",
        engine.newQObject(ArenaWidgetManager::getInstance()));

    // LinkParser namespace via evaluate
    engine.evaluate(QStringLiteral(
        "var LinkParser = {"
        "  parse: function(text) { return _bridge.parseChatLinks(text); },"
        "  parseMagnetAlias: function(text) { return _bridge.parseMagnetAlias(text); }"
        "};"
    ));

    // Create backward-compatible JS function wrappers for utility functions
    engine.evaluate(QStringLiteral(
        "function shellExec(cmd) {"
        "  var args = [];"
        "  for (var i = 1; i < arguments.length; i++) args.push(arguments[i]);"
        "  _bridge.shellExec(cmd, args);"
        "}"
        "function getMagnets() {"
        "  var files = [];"
        "  for (var i = 0; i < arguments.length; i++) files.push(arguments[i]);"
        "  return _bridge.getMagnets(files);"
        "}"
        "function printErr(msg) {"
        "  _bridge.printErr(msg);"
        "}"
        "function Import(name) {"
        "  _bridge.Import(name);"
        "}"
        "function Include(path) {"
        "  return _bridge.Include(path);"
        "}"
        "function Eval(code) {"
        "  return _bridge.Eval(code);"
        "}"
    ));

    // Create backward-compatible constructor wrappers (work with 'new' keyword)
    registerStaticMembers(engine);
    registerDynamicMembers(engine);
}

void ScriptEngine::registerStaticMembers(QJSEngine &engine){
    DEBUG_BLOCK

    static QStringList staticMembers = QStringList()
        << "AntiSpam" << "DownloadQueue" << "FavoriteHubs"
        << "Notification" << "HubManager" << "ClientManagerScript"
        << "LogManagerScript" << "FavoriteUsers" << "HashManagerScript";

    // WulforUtil and WulforSettings are already exposed as direct QObject references
    for (const auto &cl : staticMembers) {
        QString wrapper = QStringLiteral("function %1() { return _bridge.getStaticMember('%1'); }").arg(cl);
        engine.evaluate(wrapper);
    }
}

void ScriptEngine::registerDynamicMembers(QJSEngine &engine){
    DEBUG_BLOCK

    // Create JS constructor functions that delegate to bridge factory methods
    engine.evaluate(QStringLiteral(
        "function HubFrame(url, enc) { return _bridge.createHubFrame(url, enc); }"
        "function SearchFrame() { return _bridge.createSearchFrame(); }"
        "function ShellCommandRunner(cmd) {"
        "  var args = [];"
        "  for (var i = 1; i < arguments.length; i++) args.push(arguments[i]);"
        "  return _bridge.createShellCommandRunner(cmd, args);"
        "}"
        "function MainWindowScript() { return _bridge.createMainWindowScript(); }"
        "function ScriptWidget() { return _bridge.createScriptWidget(); }"
        "function QIcon(path) { return _bridge.createIcon(path); }"
    ));
}

#else // Qt5

void ScriptEngine::prepareThis(QScriptEngine &engine){
    DEBUG_BLOCK

    QScriptValue me = engine.newQObject(&engine);
    engine.globalObject().setProperty(objectName(), me);

#ifndef _WIN32
    QScriptValue scriptsPath = QScriptValue(&engine, QString(CLIENT_SCRIPTS_DIR)+QDir::separator());
#else
    QScriptValue scriptsPath = QScriptValue(&engine,
        qApp->applicationDirPath()+QDir::separator()+CLIENT_SCRIPTS_DIR+QDir::separator() );
#endif//_WIN32
    engine.globalObject().setProperty("SCRIPTS_PATH", scriptsPath, QScriptValue::ReadOnly);

    QScriptValue shellEx = engine.newFunction(shellExec);
    engine.globalObject().setProperty("shellExec", shellEx, QScriptValue::ReadOnly);

    QScriptValue getMagnet = engine.newFunction(getMagnets);
    engine.globalObject().setProperty("getMagnets", getMagnet, QScriptValue::ReadOnly);

    QScriptValue printE = engine.newFunction(printErr);
    engine.globalObject().setProperty("printErr", printE, QScriptValue::ReadOnly);

    QScriptValue import = engine.newFunction(importExtension);
    engine.globalObject().setProperty("Import", import, QScriptValue::ReadOnly);

    QScriptValue include = engine.newFunction(includeFile);
    engine.globalObject().setProperty("Include", include, QScriptValue::ReadOnly);

    QScriptValue evalStr = engine.newFunction(eval);
    engine.globalObject().setProperty("Eval", evalStr, QScriptValue::ReadOnly);

    QScriptValue MW = engine.newQObject(MainWindow::getInstance());//MainWindow already initialized
    engine.globalObject().setProperty("MainWindow", MW, QScriptValue::ReadOnly);

    QScriptValue WU = engine.newQObject(WulforUtil::getInstance());//WulforUtil already initialized
    engine.globalObject().setProperty("WulforUtil", WU, QScriptValue::ReadOnly);

    QScriptValue WS = engine.newQObject(WulforSettings::getInstance());//WulforSettings already initialized
    engine.globalObject().setProperty("WulforSettings", WS, QScriptValue::ReadOnly);

    QScriptValue linkParser = engine.newObject();
    engine.globalObject().setProperty("LinkParser", linkParser, QScriptValue::ReadOnly);
    QScriptValue linkParser_parse = engine.newFunction(parseChatLinks);
    QScriptValue linkParser_parseMagnet = engine.newFunction(parseMagnetAlias);
    linkParser.setProperty("parse", linkParser_parse, QScriptValue::ReadOnly);
    linkParser.setProperty("parseMagnetAlias", linkParser_parseMagnet, QScriptValue::ReadOnly);

    QScriptValue widgetManager = engine.newQObject(ArenaWidgetManager::getInstance());
    engine.globalObject().setProperty("WidgetManager", widgetManager, QScriptValue::ReadOnly);

    qScriptRegisterSequenceMetaType< QList<QObject*> >(&engine);
    qScriptRegisterMetaType<VarMap>(&engine, ScriptVarMapToScriptValue, ScriptVarMapFromScriptValue);
    qScriptRegisterMetaType<VarMap>(&engine, ScriptVarMapToScriptValue, ScriptVarMapFromScriptValue);

    engine.globalObject().setProperty("LinkParser", linkParser, QScriptValue::ReadOnly);

    registerStaticMembers(engine);
    registerDynamicMembers(engine);

    engine.setProcessEventsInterval(100);
}

void ScriptEngine::registerStaticMembers(QScriptEngine &engine){
    DEBUG_BLOCK

    static QStringList staticMembers = QStringList() << "AntiSpam"          << "DownloadQueue"  << "FavoriteHubs"
                                                     << "Notification"      << "HubManager"     << "ClientManagerScript"
                                                     << "LogManagerScript"  << "FavoriteUsers"  << "HashManagerScript"
                                                     << "WulforUtil"        << "WulforSettings";

    for (const auto &cl : staticMembers) {
        QScriptValue ct = engine.newFunction(staticMemberConstructor);
        ct.setProperty("className", cl, QScriptValue::ReadOnly);
        engine.globalObject().setProperty(cl, ct, QScriptValue::ReadOnly);
    }
}

void ScriptEngine::registerDynamicMembers(QScriptEngine &engine){
    DEBUG_BLOCK

    static QStringList dynamicMembers = QStringList() << "HubFrame" << "SearchFrame" << "ShellCommandRunner" << "MainWindowScript"
                                                      << "ScriptWidget";

    for (const auto &cl : dynamicMembers) {
        QScriptValue ct = engine.newFunction(dynamicMemberConstructor);
        ct.setProperty("className", cl, QScriptValue::ReadOnly);
        engine.globalObject().setProperty(cl, ct, QScriptValue::ReadOnly);
    }
}

#endif // QT_VERSION check (Qt5 vs Qt6 prepareThis/register*)

void ScriptEngine::slotWSKeyChanged(const QString &key, const QString &value){
    DEBUG_BLOCK

    if (key == WS_APP_ENABLED_SCRIPTS){
        QStringList enabled = QString(QByteArray::fromBase64(value.toLatin1())).split("\n", Qt::SkipEmptyParts);
        QMap<QString, ScriptObject*>::iterator it;

        for (const auto &script : enabled){
            it = scripts.find(script);

            if (it == scripts.end())
                loadScript(script);
        }

        QMap<QString, ScriptObject*> copy = scripts;
        for (const QString &key : copy.keys()){
            if (!enabled.contains(key)) {
                stopScript(key);
            }
        }
    }
}

void ScriptEngine::slotScriptChanged(const QString &script){
    DEBUG_BLOCK

    if (!QFile::exists(script))
        stopScript(script);
    else if (!changedFiles.contains(script)){
        changedFiles.push_back(script);
    }
}

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
// ============ Qt5 free-standing script callback functions ============

static QScriptValue shellExec(QScriptContext *ctx, QScriptEngine *engine){
    Q_UNUSED(engine);

    if (ctx->argumentCount() < 1)
        return QScriptValue();

    QString cmd = ctx->argument(0).toString();
    QStringList args = QStringList();

    for (int i = 1; i < ctx->argumentCount(); i++)
        args.push_back(ctx->argument(i).toString());

    QProcess *process = new QProcess();
    process->connect(process, qOverload<int>(&QProcess::finished), process, &QObject::deleteLater);
    process->start(cmd, args);

    return QScriptValue();
}

static QScriptValue getMagnets(QScriptContext *ctx, QScriptEngine *engine){
    Q_UNUSED(engine);

    if (ctx->argumentCount() < 1)
        return QScriptValue();

    QStringList files;

    for (int i = 0; i < ctx->argumentCount(); i++)
        files.push_back(ctx->argument(i).toString());

    QStringList magnets;

    for (const auto &f : files){
        QFile file(f);

        if (!file.exists())
            continue;

        const dcpp::TTHValue *tth = dcpp::getContext()->getHashManager()->getFileTTHif(_tq(f));

        if (tth)
            magnets.push_back(WulforUtil::getInstance()->makeMagnet(f.split(QDir::separator(), Qt::SkipEmptyParts).last(),
                                                                    file.size(),
                                                                    _q(tth->toBase32())
                                                                    ));
    }

    QScriptValue array = engine->newArray(magnets.size());

    for (int i = 0; i < magnets.length(); i++)
        array.setProperty(i, QScriptValue(magnets.at(i)));

    return array;
}

QScriptValue wulforUtilQObjectConstructor(QScriptContext *context, QScriptEngine *engine){
    Q_UNUSED(context);
    return engine->newQObject(WulforUtil::getInstance());
}

static QScriptValue staticMemberConstructor(QScriptContext *context, QScriptEngine *engine){
    QScriptValue self = context->callee();
    const QString className = self.property("className").toString();

    QObject *obj = nullptr;

    if (className == "AntiSpam"){
        if (!AntiSpam::getInstance()){
            AntiSpam::newInstance();
            AntiSpam::getInstance()->loadSettings();
            AntiSpam::getInstance()->loadLists();
        }

        obj = qobject_cast<QObject*>(AntiSpam::getInstance());
    }
    else if (className == "DownloadQueue"){
        obj = qobject_cast<QObject*>(ArenaWidgetFactory().create<dcpp::Singleton, DownloadQueue>());
    }
    else if (className == "FavoriteHubs"){
        obj = qobject_cast<QObject*>(ArenaWidgetFactory().create<dcpp::Singleton, FavoriteHubs>());
    }
    else if (className == "FavoriteUsers"){
        obj = qobject_cast<QObject*>(ArenaWidgetFactory().create<dcpp::Singleton, FavoriteUsers>());
    }
    else if (className == "Notification"){
        if (Notification::getInstance()){
            engine->globalObject().setProperty("NOTIFY_ANY", (int)Notification::ANY);

            obj = qobject_cast<QObject*>(Notification::getInstance());
        }
    }
    else if (className == "HubManager"){
        obj = qobject_cast<QObject*>(HubManager::getInstance());
    }
    else if (className == "ClientManagerScript"){
        if (!ClientManagerScript::getInstance()){
            ClientManagerScript::newInstance();

            ClientManagerScript::getInstance()->moveToThread(MainWindow::getInstance()->thread());
        }

        obj = qobject_cast<QObject*>(ClientManagerScript::getInstance());
    }
    else if (className == "HashManagerScript"){
        if (!HashManagerScript::getInstance()){
            HashManagerScript::newInstance();

            HashManagerScript::getInstance()->moveToThread(MainWindow::getInstance()->thread());
        }

        obj = qobject_cast<QObject*>(HashManagerScript::getInstance());
    }
    else if (className == "LogManagerScript"){
        if (!LogManagerScript::getInstance()){
            LogManagerScript::newInstance();

            LogManagerScript::getInstance()->moveToThread(MainWindow::getInstance()->thread());
        }

        obj = qobject_cast<QObject*>(LogManagerScript::getInstance());
    }
    else if (className == "WulforUtil"){
        QScriptValue ctor = engine->newFunction(wulforUtilQObjectConstructor);
        QScriptValue metaObject = engine->newQMetaObject(&WulforUtil::staticMetaObject, ctor);

        return metaObject;
    }
    else if (className == "WulforSettings")
        obj = qobject_cast<QObject*>(WulforSettings::getInstance());

    return engine->newQObject(obj);
}

static QScriptValue dynamicMemberConstructor(QScriptContext *context, QScriptEngine *engine){
    QScriptValue self = context->callee();
    const QString className = self.property("className").toString();

    QObject *obj = nullptr;

    if (className == "HubFrame"){
        if (context->argumentCount() == 2){
            QString hub = context->argument(0).toString();
            QString enc = context->argument(1).toString();

            HubFrame *fr = ArenaWidgetFactory().create<HubFrame, MainWindow*, QString, QString>(MainWindow::getInstance(), hub, enc);

            obj = qobject_cast<QObject*>(fr);
        }
    }
    else if (className == "SearchFrame"){
        SearchFrame *fr = ArenaWidgetFactory().create<SearchFrame>();

        obj = qobject_cast<QObject*>(fr);
    }
    else if (className == "ShellCommandRunner"){
        if (context->argumentCount() >= 1){
            QString cmd = context->argument(0).toString();
            QStringList args = QStringList();

            for (int i = 1; i < context->argumentCount(); i++)
                args.push_back(context->argument(i).toString());

            ShellCommandRunner *runner = new ShellCommandRunner(cmd, args, MainWindow::getInstance());
            runner->connect(runner, qOverload<bool, const QString &>(&ShellCommandRunner::finished), runner, &QObject::deleteLater);

            obj = qobject_cast<QObject*>(runner);
        }
    }
    else if (className == "MainWindowScript"){
        obj = qobject_cast<QObject*>(new MainWindowScript(engine, MainWindow::getInstance()));
    }
    else if (className == "ScriptWidget"){
        obj = qobject_cast<QObject*>(ArenaWidgetFactory().create<ScriptWidget>());
    }

    return engine->newQObject(obj, QScriptEngine::AutoOwnership);
}

static QScriptValue importExtension(QScriptContext *context, QScriptEngine *engine){
    if (context->argumentCount() != 1)
        return QScriptValue();

    const QString name = context->argument(0).toString();

    if (!engine->importExtension(name).isUndefined())
        qDebug() << QString("ScriptEngine> Warning! %1 not found!").arg(name).toLatin1().constData();

    return QScriptValue();
}

static QScriptValue parseChatLinks(QScriptContext *ctx, QScriptEngine *engine){
    if (ctx->argumentCount() != 1)
        return engine->undefinedValue();

    return QScriptValue(HubFrame::LinkParser::parseForLinks(ctx->argument(0).toString(), false));
}

static QScriptValue parseMagnetAlias(QScriptContext *ctx, QScriptEngine *engine) {
    if (ctx->argumentCount() != 1)
        return engine->undefinedValue();

    QString output = ctx->argument(0).toString();

    HubFrame::LinkParser::parseForMagnetAlias(output);

    return output;
}

static QScriptValue eval(QScriptContext *ctx, QScriptEngine *engine){
    if (ctx->argumentCount() < 1)
        return engine->undefinedValue();

    QString content = ctx->argument(0).toString();
    QScriptValue ret = engine->evaluate(content);

    if (engine->hasUncaughtException())
        qDebug() << engine->uncaughtExceptionBacktrace();

    return ret;
}

static QScriptValue includeFile(QScriptContext *ctx, QScriptEngine *engine){
    if (ctx->argumentCount() < 1)
        return engine->undefinedValue();

    QString path = ctx->argument(0).toString();

    QFile f(path);

    if (!(f.exists() && f.open(QIODevice::ReadOnly)))
        return engine->undefinedValue();

    QTextStream stream(&f);
    QString data = stream.readAll();

    QScriptValue ret = engine->evaluate(data);

    if (engine->hasUncaughtException())
        qDebug() << engine->uncaughtExceptionBacktrace();

    return ret;
}

QScriptValue printErr(QScriptContext *ctx, QScriptEngine *engine){
        if (ctx->argumentCount() < 1)
        return engine->undefinedValue();

    QString out = ctx->argument(0).toString();

    for (int i = 1; i < ctx->argumentCount(); i++)
        out = out.arg(ctx->argument(i).toString());

    qWarning() << qPrintable(out);

    return engine->undefinedValue();
}

QScriptValue ScriptVarMapToScriptValue(QScriptEngine* eng, const VarMap& map)
{
    QScriptValue a = eng->newObject();

    for(auto it = map.begin(); it != map.end(); ++it) {
        QString prop = it.key();
        a.setProperty(prop, qScriptValueFromValue(eng, it.value()));
    }

    return a;
}

void ScriptVarMapFromScriptValue( const QScriptValue& value, VarMap& map){
    QScriptValueIterator itr(value);

    while(itr.hasNext()){
       itr.next();
       map[itr.name()] = qscriptvalue_cast<VarMap::mapped_type>(itr.value());
    }
}

#endif // QT_VERSION < 6 (Qt5 callback functions)
