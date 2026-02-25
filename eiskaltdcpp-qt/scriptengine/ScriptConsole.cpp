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

#include "ScriptConsole.h"
#include "ScriptEngine.h"

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)

// ============ Qt6: ConsolePrinter implementation ============

ConsolePrinter::ConsolePrinter(QTextEdit *edit, QObject *parent)
    : QObject(parent), m_edit(edit) {}

void ConsolePrinter::print(const QString &text) {
    if (m_edit) m_edit->append(text);
}

void ConsolePrinter::printErr(const QString &text) {
    if (m_edit) m_edit->append(text);
}

ScriptConsole::ScriptConsole(QWidget *parent) :
    QDialog(parent)
{
    setupUi(this);

    this->setWindowFlags(Qt::Window);

    setWindowTitle(tr("Script Console"));

    ScriptEngine::getInstance()->prepareThis(engine);

    printer = new ConsolePrinter(textEdit_OUTPUT, this);
    QJSValue printerVal = engine.newQObject(printer);
    engine.globalObject().setProperty("_printer", printerVal);

    // Override print/printErr to write to console output
    engine.evaluate(QStringLiteral(
        "function print() {"
        "  var parts = [];"
        "  for (var i = 0; i < arguments.length; i++) parts.push(arguments[i]);"
        "  _printer.print(parts.join(' '));"
        "}"
        "function printErr() {"
        "  if (arguments.length < 1) return;"
        "  var msg = arguments[0];"
        "  for (var i = 1; i < arguments.length; i++) msg = msg.replace('%' + i, arguments[i]);"
        "  _printer.printErr(msg);"
        "}"
    ));

    connect(pushButton_START, &QPushButton::clicked, this, &ScriptConsole::startEvaluation);
    connect(pushButton_STOP, &QPushButton::clicked, this, &ScriptConsole::stopEvaluation);
}

void ScriptConsole::startEvaluation(){
    QJSValue result = engine.evaluate(textEdit_INPUT->toPlainText());
    if (result.isError()) {
        textEdit_OUTPUT->append(result.property("stack").toString());
    }
}

void ScriptConsole::stopEvaluation(){
    engine.setInterrupted(true);
}

#else // Qt5

static QScriptValue myPrintFunc(QScriptContext *context, QScriptEngine *engine);
static QScriptValue myPrintErrFunc(QScriptContext *context, QScriptEngine *engine);

ScriptConsole::ScriptConsole(QWidget *parent) :
    QDialog(parent)
{
    setupUi(this);
    
    this->setWindowFlags(Qt::Window);

    setWindowTitle(tr("Script Console"));

    ScriptEngine::getInstance()->prepareThis(engine);

    QScriptValue myPrint = engine.newFunction(myPrintFunc);
    myPrint.setData(engine.newQObject(textEdit_OUTPUT));
    engine.globalObject().setProperty("print", myPrint);
    
    QScriptValue myPrintErr = engine.newFunction(myPrintErrFunc);
    myPrint.setData(engine.newQObject(textEdit_OUTPUT));
    engine.globalObject().setProperty("printErr", myPrintErr);

    connect(pushButton_START, &QPushButton::clicked, this, &ScriptConsole::startEvaluation);
    connect(pushButton_STOP, &QPushButton::clicked, this, &ScriptConsole::stopEvaluation);
}

void ScriptConsole::startEvaluation(){
    engine.evaluate(textEdit_INPUT->toPlainText());
}

void ScriptConsole::stopEvaluation(){
    engine.abortEvaluation();

    if (engine.hasUncaughtException()){
        for (const auto &line : engine.uncaughtExceptionBacktrace())
            textEdit_OUTPUT->append(line);
    }
}

static QScriptValue myPrintFunc(QScriptContext *context, QScriptEngine *engine){
    QString result;

    for (int i = 0; i < context->argumentCount(); i++){
        if (i > 0)
            result.append(" ");

        result.append(context->argument(i).toString());
    }

    QScriptValue calleeData = context->callee().data();
    QTextEdit *textEdit = qobject_cast<QTextEdit*>(calleeData.toQObject());

    if (textEdit)
        textEdit->append(result);

    return engine->undefinedValue();
}

static QScriptValue myPrintErrFunc(QScriptContext *context, QScriptEngine *engine){
    if (context->argumentCount() < 1)
        return engine->undefinedValue();
    
    QString result = context->argument(0).toString();
    
    for (int i = 1; i < context->argumentCount(); i++)
        result = result.arg(context->argument(i).toString());
   
    QScriptValue calleeData = context->callee().data();
    QTextEdit *textEdit = qobject_cast<QTextEdit*>(calleeData.toQObject());

    if (textEdit)
        textEdit->append(result);

    return engine->undefinedValue();
}

#endif // QT_VERSION check
