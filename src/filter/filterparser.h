#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QQmlEngine>

#include "tokens.h"

class FilterParser : public QObject
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit FilterParser(QObject *parent = nullptr);

    Q_INVOKABLE AstNode *parse(const QString &query);
    Q_INVOKABLE bool isValid(const QString &query);
    QString lastError() const;

private:
    QList<Token> tokenize(const QString &input);
    Token nextToken();
    Token peekToken();
    void advance();
    bool expect(TokenType type);

    // Recursive descent parser
    AstNode *parseExpression();
    AstNode *parseSectionSeparator();
    AstNode *parseOr();
    AstNode *parseAnd();
    AstNode *parseUnary();
    AstNode *parseAtom();

    QList<Token> m_tokens;
    int m_pos = 0;
    QString m_lastError;
    // Iter-11: depth budget for the recursive descent. Each parseUnary /
    // parseAtom call decrements; on exhaustion we abort with a meaningful
    // error instead of overflowing the C stack. Filter query strings
    // synchronise via state.json across devices, so a poisoned query like
    // "!!!!!...!a" (10000 negations) could crash every peer that opens the
    // filter view. 256 levels is far beyond any realistic user filter while
    // staying well inside any platform's default stack.
    int m_depthRemaining = 256;
    static constexpr int kInitialDepth = 256;
};
