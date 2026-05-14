#pragma once

#include <QObject>
#include <QList>
#include <QQmlEngine>

#include "tokens.h"

class Task;
class DataStore;

class FilterEvaluator : public QObject
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit FilterEvaluator(DataStore *store, QObject *parent = nullptr);

    Q_INVOKABLE QList<Task *> evaluate(const QString &query);
    QList<Task *> evaluate(AstNode *ast, const QList<Task *> &tasks);

private:
    bool evaluateNode(AstNode *node, Task *task);
    bool evaluatePredicate(const Token &token, Task *task);

    DataStore *m_store;
};
