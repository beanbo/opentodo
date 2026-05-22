#include "filterparser.h"

#include <QRegularExpression>

FilterParser::FilterParser(QObject *parent)
    : QObject(parent)
{
}

AstNode *FilterParser::parse(const QString &query)
{
    m_lastError.clear();
    m_pos = 0;
    m_depthRemaining = kInitialDepth;

    // Iter-11: hard cap on raw input length to short-circuit pathological
    // queries before the tokenizer churns through them. 16 KiB is comfortably
    // larger than any realistic human-written filter; anything beyond is
    // either a copy-paste accident or an adversarial sync payload.
    constexpr int kMaxQueryBytes = 16 * 1024;
    if (query.size() > kMaxQueryBytes) {
        m_lastError = QStringLiteral("Query too long (%1 > %2)")
            .arg(query.size()).arg(kMaxQueryBytes);
        m_tokens.clear();
        return nullptr;
    }
    m_tokens = tokenize(query);

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
            // Read a word. Stop at all operator characters, including '!' and
            // ':' (the colon is a sigil-class character: it introduces a value
            // suffix for several keywords — search:, due:, assigned:). Keeping
            // the colon out of the word lets the keyword switch below match
            // bare names like "assigned" and "search" cleanly. F-Q2.24-B fix.
            int start = i;
            while (i < len && !input[i].isSpace() && input[i] != QLatin1Char(')') &&
                   input[i] != QLatin1Char('(') && input[i] != QLatin1Char('|') &&
                   input[i] != QLatin1Char('&') && input[i] != QLatin1Char(',') &&
                   input[i] != QLatin1Char('!') && input[i] != QLatin1Char(':'))
                ++i;
            QString word = input.mid(start, i - start).toLower();

            // Guard against an unexpected stop-only char at position `i` —
            // e.g. a bare ':' with nothing before it. Without consumption the
            // outer while-loop would not advance and we'd spin forever.
            if (word.isEmpty()) {
                ++i; // skip the stray character
                continue;
            }

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
                // Also: "no labels" → NoLabels token.
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
                } else if (nextWord == QStringLiteral("labels") ||
                           nextWord == QStringLiteral("label")) {
                    // "no labels" / "no label"
                    tokens.append(Token(TokenType::NoLabels, QStringLiteral("no labels"), startPos));
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
            } else if (word == QStringLiteral("priority")) {
                // "priority N" or "priority N..M" (range, inclusive)
                // If pattern doesn't match, rewind fully and emit identifier.
                int saved = i;
                while (i < len && input[i].isSpace()) ++i;
                int valStart = i;
                while (i < len && !input[i].isSpace() && input[i] != QLatin1Char(')') &&
                       input[i] != QLatin1Char('|') && input[i] != QLatin1Char('&') &&
                       input[i] != QLatin1Char(',') && input[i] != QLatin1Char('!'))
                    ++i;
                QString valWord = input.mid(valStart, i - valStart);

                // Try N..M (range)
                const int dots = valWord.indexOf(QStringLiteral(".."));
                if (dots > 0 && dots + 2 < valWord.length()) {
                    bool okMin = false;
                    bool okMax = false;
                    const int minVal = valWord.left(dots).toInt(&okMin);
                    const int maxVal = valWord.mid(dots + 2).toInt(&okMax);
                    if (okMin && okMax && minVal >= 1 && minVal <= 4 &&
                        maxVal >= 1 && maxVal <= 4 && minVal <= maxVal) {
                        // Encode (min, max) as a 2-int QVariantList — QPoint is not in
                        // headers we depend on, and QVariantList round-trips cleanly.
                        QVariantList rng;
                        rng.append(minVal);
                        rng.append(maxVal);
                        tokens.append(Token(TokenType::PriorityRange, valWord,
                                            QVariant::fromValue(rng), startPos));
                        continue;
                    }
                }
                // Try single int
                bool okSingle = false;
                const int p = valWord.toInt(&okSingle);
                if (okSingle && p >= 1 && p <= 4) {
                    tokens.append(Token(TokenType::Priority,
                                        QStringLiteral("p") + QString::number(p), p, startPos));
                } else {
                    // Rewind: "priority" alone (or with garbage) becomes identifier.
                    i = saved;
                    tokens.append(Token(TokenType::Identifier, word, startPos));
                }
            } else if (word == QStringLiteral("due")) {
                // "due"     → HasDue with value -1 (any due date)
                // "due: N"  → HasDue with value N  (within next N days inclusive)
                // "due:N"   would be a single word "due:N"; that is handled below.
                int look = i;
                while (look < len && input[look].isSpace()) ++look;
                if (look < len && input[look] == QLatin1Char(':')) {
                    int p = look + 1;
                    while (p < len && input[p].isSpace()) ++p;
                    int numStart = p;
                    while (p < len && input[p].isDigit()) ++p;
                    if (p > numStart) {
                        const int n = input.mid(numStart, p - numStart).toInt();
                        i = p;
                        tokens.append(Token(TokenType::HasDue,
                                            QStringLiteral("due:") + QString::number(n),
                                            n, startPos));
                    } else {
                        // "due:" with no number — treat as "any due date" and
                        // consume the colon so it doesn't poison the next token.
                        i = look + 1;
                        tokens.append(Token(TokenType::HasDue, QStringLiteral("due"), -1, startPos));
                    }
                } else {
                    tokens.append(Token(TokenType::HasDue, QStringLiteral("due"), -1, startPos));
                }
            } else if (word == QStringLiteral("subtask")) {
                tokens.append(Token(TokenType::Subtask, word, startPos));
            } else if (word == QStringLiteral("view")) {
                // "view all" → ViewAll. Anything else: rewind and emit "view"
                // as an identifier so we don't break content matching.
                int saved = i;
                while (i < len && input[i].isSpace()) ++i;
                int nStart = i;
                while (i < len && !input[i].isSpace() && input[i] != QLatin1Char(')') &&
                       input[i] != QLatin1Char('|') && input[i] != QLatin1Char('&') &&
                       input[i] != QLatin1Char(',') && input[i] != QLatin1Char('!'))
                    ++i;
                QString nextW = input.mid(nStart, i - nStart).toLower();
                if (nextW == QStringLiteral("all")) {
                    tokens.append(Token(TokenType::ViewAll, QStringLiteral("view all"), startPos));
                } else {
                    i = saved;
                    tokens.append(Token(TokenType::Identifier, word, startPos));
                }
            } else if (word == QStringLiteral("all")) {
                tokens.append(Token(TokenType::ViewAll, word, startPos));
            } else if (word == QStringLiteral("search")) {
                // F-Q2.24-B: colon is now a word stop, so the bare word is
                // "search" rather than "search:". Consume an optional ':' and
                // optional whitespace, then read the search term up to the
                // next top-level operator. The colon itself is required —
                // a bare "search" without ':' falls through to Identifier.
                if (i < len && input[i] == QLatin1Char(':')) {
                    ++i; // consume ':'
                    while (i < len && input[i].isSpace()) ++i;
                    int searchStart = i;
                    while (i < len &&
                           input[i] != QLatin1Char('|') && input[i] != QLatin1Char('&') &&
                           input[i] != QLatin1Char(',') && input[i] != QLatin1Char(')'))
                        ++i;
                    QString searchTerm = input.mid(searchStart, i - searchStart).trimmed();
                    tokens.append(Token(TokenType::Search, searchTerm, startPos));
                } else {
                    // "search" without a colon — treat as an identifier so
                    // content-substring matching still applies for a user who
                    // typed it as a literal word.
                    tokens.append(Token(TokenType::Identifier, word, startPos));
                }
            } else if (word == QStringLiteral("created")) {
                // Look for "before", "after", "today", or "this week"
                while (i < len && input[i].isSpace()) ++i;
                int kStart = i;
                while (i < len && !input[i].isSpace() && input[i] != QLatin1Char(')') &&
                       input[i] != QLatin1Char('|') && input[i] != QLatin1Char('&') &&
                       input[i] != QLatin1Char(','))
                    ++i;
                QString keyword = input.mid(kStart, i - kStart).toLower();

                if (keyword == QStringLiteral("today")) {
                    tokens.append(Token(TokenType::CreatedToday, QStringLiteral("created today"), startPos));
                } else if (keyword == QStringLiteral("this")) {
                    // peek next word for "week"
                    int afterThis = i;
                    while (afterThis < len && input[afterThis].isSpace()) ++afterThis;
                    int wkStart = afterThis;
                    while (afterThis < len && !input[afterThis].isSpace() &&
                           input[afterThis] != QLatin1Char(')') &&
                           input[afterThis] != QLatin1Char('|') &&
                           input[afterThis] != QLatin1Char('&') &&
                           input[afterThis] != QLatin1Char(','))
                        ++afterThis;
                    QString maybeWeek = input.mid(wkStart, afterThis - wkStart).toLower();
                    if (maybeWeek == QStringLiteral("week")) {
                        i = afterThis;
                        tokens.append(Token(TokenType::CreatedThisWeek, QStringLiteral("created this week"), startPos));
                    } else {
                        // Not "this week" — rewind and treat "created" as identifier.
                        tokens.append(Token(TokenType::Identifier, word, startPos));
                        i = kStart;
                    }
                } else if (keyword == QStringLiteral("before") || keyword == QStringLiteral("after")) {
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
                    } else {
                        tokens.append(Token(TokenType::CreatedAfter, dateVal, startPos));
                    }
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
    // Iter-11: bound the recursion depth. A query like "!!!!!…!a" with 50k
    // negations would otherwise overflow the C stack (synced filter strings
    // arrive from peers, so a poisoned query crashes every device opening
    // the filter view).
    if (--m_depthRemaining < 0) {
        m_lastError = QStringLiteral("Filter expression too deeply nested");
        ++m_depthRemaining; // restore for callers that may share the budget
        return nullptr;
    }
    AstNode *result = nullptr;
    if (peekToken().type == TokenType::Not) {
        advance();
        AstNode *child = parseUnary();
        if (!child) { ++m_depthRemaining; return nullptr; }
        result = AstNode::makeNot(child);
    } else {
        result = parseAtom();
    }
    ++m_depthRemaining;
    return result;
}

// Atom := '(' Expression ')' | predicate
AstNode *FilterParser::parseAtom()
{
    if (peekToken().type == TokenType::LeftParen) {
        // Iter-11: parentheses descend back into parseExpression, which can
        // recurse arbitrarily deep on "(((…)))". Share the same depth budget.
        if (--m_depthRemaining < 0) {
            m_lastError = QStringLiteral("Filter expression too deeply nested");
            ++m_depthRemaining;
            return nullptr;
        }
        advance();
        AstNode *expr = parseExpression();
        ++m_depthRemaining;
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
    case TokenType::PriorityRange:
    case TokenType::Project:
    case TokenType::Label:
    case TokenType::Section:
    case TokenType::Search:
    case TokenType::CreatedBefore:
    case TokenType::CreatedAfter:
    case TokenType::CreatedToday:
    case TokenType::CreatedThisWeek:
    case TokenType::Recurring:
    case TokenType::NotRecurring:  // B-4.42: now handled
    case TokenType::HasDeadline:
    case TokenType::Assigned:      // F-4.43: now handled
    case TokenType::Completed:
    case TokenType::HasDue:
    case TokenType::Subtask:
    case TokenType::ViewAll:
    case TokenType::NoLabels:
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
