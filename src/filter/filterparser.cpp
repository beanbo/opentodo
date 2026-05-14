#include "filterparser.h"

#include <QRegularExpression>

FilterParser::FilterParser(QObject *parent)
    : QObject(parent)
{
}

AstNode *FilterParser::parse(const QString &query)
{
    m_lastError.clear();
    m_tokens = tokenize(query);
    m_pos = 0;

    if (m_tokens.isEmpty()) {
        m_lastError = QStringLiteral("Empty query");
        return nullptr;
    }

    AstNode *result = parseExpression();

    if (m_pos < m_tokens.size() && m_tokens[m_pos].type != TokenType::EndOfInput) {
        m_lastError = QStringLiteral("Unexpected token at position %1: '%2'")
            .arg(m_tokens[m_pos].position)
            .arg(m_tokens[m_pos].text);
        delete result;
        return nullptr;
    }

    return result;
}

bool FilterParser::isValid(const QString &query)
{
    auto *ast = parse(query);
    if (ast) {
        delete ast;
        return true;
    }
    return false;
}

QString FilterParser::lastError() const
{
    return m_lastError;
}

QList<Token> FilterParser::tokenize(const QString &input)
{
    QList<Token> tokens;
    int i = 0;
    const int len = input.length();

    while (i < len) {
        // Skip whitespace
        while (i < len && input[i].isSpace()) ++i;
        if (i >= len) break;

        const QChar ch = input[i];
        const int startPos = i;

        if (ch == QLatin1Char('|')) {
            tokens.append(Token(TokenType::Or, QStringLiteral("|"), startPos));
            ++i;
        } else if (ch == QLatin1Char('&')) {
            tokens.append(Token(TokenType::And, QStringLiteral("&"), startPos));
            ++i;
        } else if (ch == QLatin1Char('!')) {
            // B-4.42 FIX: when '!' is immediately followed by "recurring" (no space),
            // emit a single NotRecurring token instead of Not + Recurring.
            if (i + 1 < len) {
                const QString tail = input.mid(i + 1);
                if (tail.startsWith(QStringLiteral("recurring"), Qt::CaseInsensitive)) {
                    const int afterRecurring = i + 1 + 9; // len("recurring") == 9
                    // Make sure "recurring" is a complete word (followed by space, end, or operator)
                    bool wordEnd = (afterRecurring >= len)
                                  || input[afterRecurring].isSpace()
                                  || input[afterRecurring] == QLatin1Char(')')
                                  || input[afterRecurring] == QLatin1Char('|')
                                  || input[afterRecurring] == QLatin1Char('&')
                                  || input[afterRecurring] == QLatin1Char(',');
                    if (wordEnd) {
                        tokens.append(Token(TokenType::NotRecurring, QStringLiteral("!recurring"), startPos));
                        i = afterRecurring;
                        continue;
                    }
                }
            }
            tokens.append(Token(TokenType::Not, QStringLiteral("!"), startPos));
            ++i;
        } else if (ch == QLatin1Char('(')) {
            tokens.append(Token(TokenType::LeftParen, QStringLiteral("("), startPos));
            ++i;
        } else if (ch == QLatin1Char(')')) {
            tokens.append(Token(TokenType::RightParen, QStringLiteral(")"), startPos));
            ++i;
        } else if (ch == QLatin1Char(',')) {
            tokens.append(Token(TokenType::Comma, QStringLiteral(","), startPos));
            ++i;
        } else if (ch == QLatin1Char('#')) {
            // Project reference
            ++i;
            int start = i;
            while (i < len && !input[i].isSpace() && input[i] != QLatin1Char(')') &&
                   input[i] != QLatin1Char('|') && input[i] != QLatin1Char('&') &&
                   input[i] != QLatin1Char(','))
                ++i;
            tokens.append(Token(TokenType::Project, input.mid(start, i - start), startPos));
        } else if (ch == QLatin1Char('@')) {
            // Label reference
            ++i;
            int start = i;
            while (i < len && !input[i].isSpace() && input[i] != QLatin1Char(')') &&
                   input[i] != QLatin1Char('|') && input[i] != QLatin1Char('&') &&
                   input[i] != QLatin1Char(','))
                ++i;
            tokens.append(Token(TokenType::Label, input.mid(start, i - start), startPos));
        } else if (ch == QLatin1Char('/')) {
            // Section reference
            ++i;
            int start = i;
            while (i < len && !input[i].isSpace() && input[i] != QLatin1Char(')') &&
                   input[i] != QLatin1Char('|') && input[i] != QLatin1Char('&') &&
                   input[i] != QLatin1Char(','))
                ++i;
            tokens.append(Token(TokenType::Section, input.mid(start, i - start), startPos));
        } else {
            // Read a word (stop at all operator characters, including '!')
            int start = i;
            while (i < len && !input[i].isSpace() && input[i] != QLatin1Char(')') &&
                   input[i] != QLatin1Char('(') && input[i] != QLatin1Char('|') &&
                   input[i] != QLatin1Char('&') && input[i] != QLatin1Char(',') &&
                   input[i] != QLatin1Char('!'))
                ++i;
            QString word = input.mid(start, i - start).toLower();

            if (word == QStringLiteral("today")) {
                tokens.append(Token(TokenType::Today, word, startPos));
            } else if (word == QStringLiteral("tomorrow")) {
                tokens.append(Token(TokenType::Tomorrow, word, startPos));
            } else if (word == QStringLiteral("yesterday")) {
                tokens.append(Token(TokenType::Yesterday, word, startPos));
            } else if (word == QStringLiteral("overdue")) {
                tokens.append(Token(TokenType::Overdue, word, startPos));
            } else if (word == QStringLiteral("recurring")) {
                tokens.append(Token(TokenType::Recurring, word, startPos));
            } else if (word == QStringLiteral("deadline") ||
                       word == QStringLiteral("дедлайн") ||
                       word == QStringLiteral("дедлайном")) {
                // Single-word forms: "deadline", "дедлайн", "дедлайном"
                tokens.append(Token(TokenType::HasDeadline, word, startPos));
            } else if (word == QStringLiteral("has") ||
                       word == QStringLiteral("with") ||
                       word == QStringLiteral("с") ||
                       word == QStringLiteral("есть")) {
                // Multi-word forms: "has deadline", "with deadline",
                // "с дедлайном", "есть дедлайн".
                int saved = i;
                while (i < len && input[i].isSpace()) ++i;
                int nextStart = i;
                while (i < len && !input[i].isSpace() && input[i] != QLatin1Char(')') &&
                       input[i] != QLatin1Char('|') && input[i] != QLatin1Char('&') &&
                       input[i] != QLatin1Char(',') && input[i] != QLatin1Char('!'))
                    ++i;
                QString nextWord = input.mid(nextStart, i - nextStart).toLower();
                // Normalize ё → е for Russian tolerance.
                nextWord.replace(QChar(0x0451), QChar(0x0435));

                const bool nextIsDeadline =
                    nextWord == QStringLiteral("deadline") ||
                    nextWord == QStringLiteral("дедлайн") ||
                    nextWord == QStringLiteral("дедлайном");

                if (nextIsDeadline) {
                    tokens.append(Token(TokenType::HasDeadline,
                                        word + QLatin1Char(' ') + nextWord, startPos));
                } else {
                    // Rewind: emit current word as identifier, let next iteration handle it.
                    i = saved;
                    tokens.append(Token(TokenType::Identifier, word, startPos));
                }
            } else if (word == QStringLiteral("completed")) {
                tokens.append(Token(TokenType::Completed, word, startPos));
            } else if (word == QStringLiteral("assigned")) {
                // F-4.43 FIX: tokenize "assigned" and optional ": <name>" clause.
                // Consume optional ": <name>" or ": me" suffix.
                // Forms: "assigned", "assigned: me", "assigned: <name>"
                int saved = i;
                while (i < len && input[i].isSpace()) ++i;
                if (i < len && input[i] == QLatin1Char(':')) {
                    ++i; // consume ':'
                    while (i < len && input[i].isSpace()) ++i;
                    int nameStart = i;
                    while (i < len && !input[i].isSpace() && input[i] != QLatin1Char(')') &&
                           input[i] != QLatin1Char('|') && input[i] != QLatin1Char('&') &&
                           input[i] != QLatin1Char(','))
                        ++i;
                    QString assignee = input.mid(nameStart, i - nameStart).toLower();
                    // Store assignee name in token text.
                    tokens.append(Token(TokenType::Assigned, assignee, startPos));
                } else {
                    i = saved; // no colon: "assigned" alone (means assigned: me)
                    tokens.append(Token(TokenType::Assigned, QStringLiteral("me"), startPos));
                }
            } else if (word == QStringLiteral("no")) {
                // B-4.41 FIX: "no date" is valid; "no due date" is valid only if "date"
                // follows "due".  If "no due" is NOT followed by "date", rewind fully.
                while (i < len && input[i].isSpace()) ++i;
                int nextStart = i;
                while (i < len && !input[i].isSpace() && input[i] != QLatin1Char(')') &&
                       input[i] != QLatin1Char('|') && input[i] != QLatin1Char('&') &&
                       input[i] != QLatin1Char(',') && input[i] != QLatin1Char('!'))
                    ++i;
                QString nextWord = input.mid(nextStart, i - nextStart).toLower();

                if (nextWord == QStringLiteral("date")) {
                    // "no date"
                    tokens.append(Token(TokenType::NoDate, QStringLiteral("no date"), startPos));
                } else if (nextWord == QStringLiteral("due")) {
                    // peek at the word after "due"
                    int afterDue = i;
                    while (afterDue < len && input[afterDue].isSpace()) ++afterDue;
                    int dateStart = afterDue;
                    while (afterDue < len && !input[afterDue].isSpace() &&
                           input[afterDue] != QLatin1Char(')') &&
                           input[afterDue] != QLatin1Char('|') &&
                           input[afterDue] != QLatin1Char('&') &&
                           input[afterDue] != QLatin1Char(','))
                        ++afterDue;
                    QString maybeDate = input.mid(dateStart, afterDue - dateStart).toLower();

                    if (maybeDate == QStringLiteral("date")) {
                        // "no due date" — consume "date"
                        i = afterDue;
                        tokens.append(Token(TokenType::NoDate, QStringLiteral("no due date"), startPos));
                    } else {
                        // "no due <something-else>" — rewind: emit "no" as identifier,
                        // let "due" tokenize on the next iteration.
                        i = nextStart;
                        tokens.append(Token(TokenType::Identifier, word, startPos));
                    }
                } else {
                    i = nextStart; // rewind — "no <other>"
                    tokens.append(Token(TokenType::Identifier, word, startPos));
                }
            } else if (word.startsWith(QStringLiteral("p")) && word.length() == 2 &&
                       word[1].isDigit()) {
                int priority = word[1].digitValue();
                if (priority >= 1 && priority <= 4) {
                    tokens.append(Token(TokenType::Priority, word, priority, startPos));
                } else {
                    tokens.append(Token(TokenType::Identifier, word, startPos));
                }
            } else if (word == QStringLiteral("search:")) {
                // B-4.48 FIX: stop search term at ',', ')', '|', '&'.
                while (i < len && input[i].isSpace()) ++i;
                int searchStart = i;
                while (i < len &&
                       input[i] != QLatin1Char('|') && input[i] != QLatin1Char('&') &&
                       input[i] != QLatin1Char(',') && input[i] != QLatin1Char(')'))
                    ++i;
                QString searchTerm = input.mid(searchStart, i - searchStart).trimmed();
                tokens.append(Token(TokenType::Search, searchTerm, startPos));
            } else if (word == QStringLiteral("created")) {
                // Look for "before" or "after"
                while (i < len && input[i].isSpace()) ++i;
                int kStart = i;
                while (i < len && !input[i].isSpace() && input[i] != QLatin1Char(')') &&
                       input[i] != QLatin1Char('|') && input[i] != QLatin1Char('&') &&
                       input[i] != QLatin1Char(','))
                    ++i;
                QString keyword = input.mid(kStart, i - kStart).toLower();

                // Read the date value
                while (i < len && input[i].isSpace()) ++i;
                int dStart = i;
                while (i < len && !input[i].isSpace() && input[i] != QLatin1Char(')') &&
                       input[i] != QLatin1Char('|') && input[i] != QLatin1Char('&') &&
                       input[i] != QLatin1Char(','))
                    ++i;
                QString dateVal = input.mid(dStart, i - dStart);

                if (keyword == QStringLiteral("before")) {
                    tokens.append(Token(TokenType::CreatedBefore, dateVal, startPos));
                } else if (keyword == QStringLiteral("after")) {
                    tokens.append(Token(TokenType::CreatedAfter, dateVal, startPos));
                } else {
                    tokens.append(Token(TokenType::Identifier, word, startPos));
                    i = kStart; // rewind
                }
            } else if (word == QStringLiteral("next")) {
                // F-4.47 FIX: "next N days" → Days token with value N.
                // If pattern doesn't match, rewind and emit Identifier.
                int saved = i;
                while (i < len && input[i].isSpace()) ++i;
                int numStart = i;
                while (i < len && input[i].isDigit()) ++i;
                if (i > numStart) {
                    // got a number
                    QString numStr = input.mid(numStart, i - numStart);
                    int num = numStr.toInt();
                    while (i < len && input[i].isSpace()) ++i;
                    int unitStart = i;
                    while (i < len && !input[i].isSpace() && input[i] != QLatin1Char(')') &&
                           input[i] != QLatin1Char('|') && input[i] != QLatin1Char('&') &&
                           input[i] != QLatin1Char(','))
                        ++i;
                    QString unit = input.mid(unitStart, i - unitStart).toLower();
                    if (unit == QStringLiteral("days") || unit == QStringLiteral("day")) {
                        tokens.append(Token(TokenType::Days, numStr, num, startPos));
                    } else {
                        i = saved;
                        tokens.append(Token(TokenType::Identifier, word, startPos));
                    }
                } else {
                    i = saved;
                    tokens.append(Token(TokenType::Identifier, word, startPos));
                }
            } else if (word == QStringLiteral("before") || word == QStringLiteral("after")) {
                // due before/after
                while (i < len && input[i].isSpace()) ++i;
                int dStart = i;
                while (i < len && !input[i].isSpace() && input[i] != QLatin1Char(')') &&
                       input[i] != QLatin1Char('|') && input[i] != QLatin1Char('&') &&
                       input[i] != QLatin1Char(','))
                    ++i;
                QString dateVal = input.mid(dStart, i - dStart);
                tokens.append(Token(word == QStringLiteral("before") ? TokenType::Before : TokenType::After,
                                    dateVal, startPos));
            } else {
                // Check for "N days"
                bool isNum = false;
                int num = word.toInt(&isNum);
                if (isNum) {
                    // Peek for "days"
                    int saved = i;
                    while (i < len && input[i].isSpace()) ++i;
                    int wStart = i;
                    while (i < len && !input[i].isSpace() && input[i] != QLatin1Char(')') &&
                           input[i] != QLatin1Char('|') && input[i] != QLatin1Char('&') &&
                           input[i] != QLatin1Char(','))
                        ++i;
                    QString nextW = input.mid(wStart, i - wStart).toLower();
                    if (nextW == QStringLiteral("days") || nextW == QStringLiteral("day")) {
                        tokens.append(Token(TokenType::Days, word, num, startPos));
                    } else {
                        i = saved;
                        tokens.append(Token(TokenType::NumberLiteral, word, num, startPos));
                    }
                } else {
                    tokens.append(Token(TokenType::Identifier, word, startPos));
                }
            }
        }
    }

    tokens.append(Token(TokenType::EndOfInput, QString(), len));
    return tokens;
}

Token FilterParser::nextToken()
{
    if (m_pos < m_tokens.size())
        return m_tokens[m_pos++];
    return Token(TokenType::EndOfInput, QString());
}

Token FilterParser::peekToken()
{
    if (m_pos < m_tokens.size())
        return m_tokens[m_pos];
    return Token(TokenType::EndOfInput, QString());
}

void FilterParser::advance()
{
    if (m_pos < m_tokens.size()) ++m_pos;
}

bool FilterParser::expect(TokenType type)
{
    if (peekToken().type == type) {
        advance();
        return true;
    }
    return false;
}

// Expression := SectionSeparator
AstNode *FilterParser::parseExpression()
{
    return parseSectionSeparator();
}

// SectionSeparator := Or (',' Or)*
AstNode *FilterParser::parseSectionSeparator()
{
    AstNode *left = parseOr();
    if (!left) return nullptr;

    while (peekToken().type == TokenType::Comma) {
        advance();
        AstNode *right = parseOr();
        if (!right) { delete left; return nullptr; }
        left = AstNode::makeSectionSep(left, right);
    }

    return left;
}

// Or := And ('|' And)*
AstNode *FilterParser::parseOr()
{
    AstNode *left = parseAnd();
    if (!left) return nullptr;

    while (peekToken().type == TokenType::Or) {
        advance();
        AstNode *right = parseAnd();
        if (!right) { delete left; return nullptr; }
        left = AstNode::makeOr(left, right);
    }

    return left;
}

// And := Unary ('&' Unary)*
AstNode *FilterParser::parseAnd()
{
    AstNode *left = parseUnary();
    if (!left) return nullptr;

    while (peekToken().type == TokenType::And) {
        advance();
        AstNode *right = parseUnary();
        if (!right) { delete left; return nullptr; }
        left = AstNode::makeAnd(left, right);
    }

    return left;
}

// Unary := '!' Unary | Atom
AstNode *FilterParser::parseUnary()
{
    if (peekToken().type == TokenType::Not) {
        advance();
        AstNode *child = parseUnary();
        if (!child) return nullptr;
        return AstNode::makeNot(child);
    }

    return parseAtom();
}

// Atom := '(' Expression ')' | predicate
AstNode *FilterParser::parseAtom()
{
    if (peekToken().type == TokenType::LeftParen) {
        advance();
        AstNode *expr = parseExpression();
        if (!expr) return nullptr;
        if (!expect(TokenType::RightParen)) {
            m_lastError = QStringLiteral("Expected ')' at position %1").arg(peekToken().position);
            delete expr;
            return nullptr;
        }
        return expr;
    }

    Token tok = peekToken();
    switch (tok.type) {
    case TokenType::Today:
    case TokenType::Tomorrow:
    case TokenType::Yesterday:
    case TokenType::Overdue:
    case TokenType::NoDate:
    case TokenType::NoDueDate:
    case TokenType::Days:
    case TokenType::Before:
    case TokenType::After:
    case TokenType::Priority:
    case TokenType::Project:
    case TokenType::Label:
    case TokenType::Section:
    case TokenType::Search:
    case TokenType::CreatedBefore:
    case TokenType::CreatedAfter:
    case TokenType::Recurring:
    case TokenType::NotRecurring:  // B-4.42: now handled
    case TokenType::HasDeadline:
    case TokenType::Assigned:      // F-4.43: now handled
    case TokenType::Completed:
    case TokenType::Identifier:
    case TokenType::Date:
        advance();
        return AstNode::makePredicate(tok);
    default:
        m_lastError = QStringLiteral("Unexpected token '%1' at position %2")
            .arg(tok.text).arg(tok.position);
        return nullptr;
    }
}
