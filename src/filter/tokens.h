#pragma once

#include <QString>
#include <QVariant>

enum class TokenType {
    // Operators
    Or,             // |
    And,            // &
    Not,            // !
    LeftParen,      // (
    RightParen,     // )
    Comma,          // , (section separator)

    // Date predicates
    Today,
    Tomorrow,
    Yesterday,
    Overdue,
    NoDate,
    NoDueDate,
    Days,           // "N days" or "next N days"
    Before,         // "before <date>"
    After,          // "after <date>"
    Date,           // literal date YYYY-MM-DD

    // Priority
    Priority,       // p1, p2, p3, p4 — also "priority N" (single value)
    PriorityRange,  // "priority N..M" — token.value is QVariantList[min, max]

    // Entity references
    Project,        // #ProjectName
    Label,          // @LabelName
    Section,        // /SectionName

    // Search
    Search,         // search: keyword

    // Created date
    CreatedBefore,
    CreatedAfter,
    CreatedToday,    // "created today"
    CreatedThisWeek, // "created this week"

    // Recurring
    Recurring,
    NotRecurring,

    // Deadline
    HasDeadline,

    // Assigned (for future multi-user)
    Assigned,

    // Completed
    Completed,

    // Has-any-due-date / due-within-N-days (shorthand-style "due" / "due: N")
    // token.value holds N (-1 == "any due date", N>=0 == within N days)
    HasDue,

    // Subtask: task has a non-empty parentId
    Subtask,

    // "view all" / "all" — matches every (non-deleted) task; lets a saved
    // filter ignore the implicit project/inbox scoping the UI may apply.
    ViewAll,

    // "label none" / "no labels" — task with empty labels array
    NoLabels,

    // Literal / identifier
    Identifier,
    StringLiteral,
    NumberLiteral,

    // Special
    EndOfInput,
    Invalid
};

struct Token {
    TokenType type = TokenType::Invalid;
    QString text;
    QVariant value;
    int position = 0;

    Token() = default;
    Token(TokenType t, const QString &txt, int pos = 0)
        : type(t), text(txt), position(pos) {}
    Token(TokenType t, const QString &txt, const QVariant &val, int pos = 0)
        : type(t), text(txt), value(val), position(pos) {}
};

// AST Node types for the parsed filter expression
enum class AstNodeType {
    Or,
    And,
    Not,
    Predicate,
    SectionSeparator
};

struct AstNode {
    AstNodeType type;
    Token token;                         // for predicates
    QList<AstNode *> children;           // for operators

    ~AstNode() { qDeleteAll(children); }

    static AstNode *makeOr(AstNode *left, AstNode *right) {
        auto *n = new AstNode{AstNodeType::Or, {}, {left, right}};
        return n;
    }
    static AstNode *makeAnd(AstNode *left, AstNode *right) {
        auto *n = new AstNode{AstNodeType::And, {}, {left, right}};
        return n;
    }
    static AstNode *makeNot(AstNode *child) {
        auto *n = new AstNode{AstNodeType::Not, {}, {child}};
        return n;
    }
    static AstNode *makePredicate(const Token &tok) {
        auto *n = new AstNode{AstNodeType::Predicate, tok, {}};
        return n;
    }
    static AstNode *makeSectionSep(AstNode *left, AstNode *right) {
        auto *n = new AstNode{AstNodeType::SectionSeparator, {}, {left, right}};
        return n;
    }
};
