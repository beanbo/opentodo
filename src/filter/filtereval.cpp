#include "filtereval.h"
#include "filterparser.h"
#include "storage/datastore.h"
#include "model/task.h"
#include "model/project.h"
#include "model/section.h"
#include "model/label.h"
#include "utils/datetime.h"
#include "utils/settingskeys.h"
#include "utils/textsearch.h"
#include "nlp/dateparser.h"

#include <QDate>
#include <QDateTime>

FilterEvaluator::FilterEvaluator(DataStore *store, QObject *parent)
    : QObject(parent)
    , m_store(store)
{
}

QList<Task *> FilterEvaluator::evaluate(const QString &query)
{
    FilterParser parser;
    AstNode *ast = parser.parse(query);
    if (!ast) return {};

    QList<Task *> allTasks = m_store->allTasks();
    QList<Task *> result = evaluate(ast, allTasks);
    delete ast;
    return result;
}

QList<Task *> FilterEvaluator::evaluate(AstNode *ast, const QList<Task *> &tasks)
{
    QList<Task *> result;
    for (auto *task : tasks) {
        if (evaluateNode(ast, task))
            result.append(task);
    }
    return result;
}

bool FilterEvaluator::evaluateNode(AstNode *node, Task *task)
{
    if (!node) return false;

    switch (node->type) {
    case AstNodeType::Or:
        return evaluateNode(node->children.value(0), task) ||
               evaluateNode(node->children.value(1), task);

    case AstNodeType::And:
        return evaluateNode(node->children.value(0), task) &&
               evaluateNode(node->children.value(1), task);

    case AstNodeType::Not:
        return !evaluateNode(node->children.value(0), task);

    case AstNodeType::Predicate:
        return evaluatePredicate(node->token, task);

    case AstNodeType::SectionSeparator:
        // Comma acts as OR for display sections
        return evaluateNode(node->children.value(0), task) ||
               evaluateNode(node->children.value(1), task);
    }

    return false;
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace {

// B-4.44/45 FIX: resolve the effective due QDate for a task.
// If dueDate is set, use it.  If only dueDatetime is set, extract its date.
static QDate effectiveDueDate(const Task *task)
{
    const QString dueDate = task->dueDate();
    if (!dueDate.isEmpty())
        return DateTimeHelper::fromDateString(dueDate);

    // Fall back to parsing the datetime field.
    const QString dueDt = task->dueDatetime();
    if (!dueDt.isEmpty()) {
        const QDateTime dt = QDateTime::fromString(dueDt, Qt::ISODate);
        if (dt.isValid())
            return dt.date();
    }
    return QDate(); // no due information at all
}

// B-4.39/40 FIX: resolve a date string from a filter token through DateParser
// (supporting natural-language strings like "today") with ISO fallback.
static QDate resolveDateToken(const QString &text)
{
    if (text.isEmpty()) return QDate();

    // Try natural-language parse (English then Russian).
    DateParseResult enRes = DateParser::parseEnglish(text, QDate::currentDate());
    if (enRes.valid && !enRes.date.isEmpty())
        return QDate::fromString(enRes.date, Qt::ISODate);

    DateParseResult ruRes = DateParser::parseRussian(text, QDate::currentDate());
    if (ruRes.valid && !ruRes.date.isEmpty())
        return QDate::fromString(ruRes.date, Qt::ISODate);

    // Fallback: plain ISO date.
    return DateTimeHelper::fromDateString(text);
}

} // namespace

// ---------------------------------------------------------------------------
// FilterEvaluator::evaluatePredicate
// ---------------------------------------------------------------------------

bool FilterEvaluator::evaluatePredicate(const Token &token, Task *task)
{
    const QDate today = QDate::currentDate();

    switch (token.type) {
    case TokenType::Today: {
        // B-4.44/45 FIX: consider datetime-only tasks.
        const QDate due = effectiveDueDate(task);
        return due.isValid() && due == today;
    }

    case TokenType::Tomorrow: {
        // B-4.44/45 FIX
        const QDate due = effectiveDueDate(task);
        return due.isValid() && due == today.addDays(1);
    }

    case TokenType::Yesterday: {
        // B-4.44/45 FIX
        const QDate due = effectiveDueDate(task);
        return due.isValid() && due == today.addDays(-1);
    }

    case TokenType::Overdue: {
        // Iter-night-17: use TIME-PRECISE comparison for datetime-only tasks,
        // matching DataStore::overdueTasks (datastore.cpp:611-627) and the
        // DateTimeHelper::isOverdue(dateStr, datetimeStr) overload that
        // already handles this case.
        //
        // Pre-fix this branch went through effectiveDueDate() which truncates
        // dueDatetime to its date component. A task due "today at 09:00"
        // viewed at 15:00 the same day had due.date()==today, so
        // `due < today` was FALSE and the filter said "not overdue" — even
        // though the sidebar Overdue/Today badge (post-iter-night-16) and
        // DataStore::overdueTasks both flagged it. The two surfaces
        // disagreed for the entire calendar day after a datetime-only
        // task's missed moment.
        //
        // Today/Tomorrow/Yesterday tokens keep using effectiveDueDate
        // (calendar-day semantics — "today" means anywhere in this day
        // regardless of time-of-day). Only `overdue` semantically requires
        // sub-day precision.
        if (task->isCompleted()) return false;
        const QString dueDate = task->dueDate();
        if (!dueDate.isEmpty()) {
            const QDate d = DateTimeHelper::fromDateString(dueDate);
            return d.isValid() && d < today;
        }
        const QString dueDt = task->dueDatetime();
        if (dueDt.isEmpty()) return false;
        const QDateTime dt = DateTimeHelper::fromIsoString(dueDt);
        return dt.isValid() && dt < QDateTime::currentDateTime();
    }

    case TokenType::NoDate:
    case TokenType::NoDueDate:
        // No due if both dueDate and dueDatetime are empty.
        return task->dueDate().isEmpty() && task->dueDatetime().isEmpty();

    case TokenType::Days: {
        // B-4.46 FIX: "7 days" should include days [today, today+6], not today+7.
        // Use endDate = today.addDays(numDays - 1).
        const int numDays = token.value.toInt();
        const QDate due = effectiveDueDate(task);
        if (!due.isValid()) return false;
        const QDate endDate = today.addDays(numDays - 1);
        return due >= today && due <= endDate;
    }

    case TokenType::Before: {
        const QDate due = effectiveDueDate(task);
        // B-4.39/40 FIX: route through DateParser.
        const QDate beforeDate = resolveDateToken(token.text);
        return due.isValid() && beforeDate.isValid() && due < beforeDate;
    }

    case TokenType::After: {
        const QDate due = effectiveDueDate(task);
        // B-4.39/40 FIX: route through DateParser.
        const QDate afterDate = resolveDateToken(token.text);
        return due.isValid() && afterDate.isValid() && due > afterDate;
    }

    case TokenType::Priority:
        return task->priority() == token.value.toInt();

    case TokenType::PriorityRange: {
        // token.value is a QVariantList of two ints (min, max), inclusive.
        const QVariantList rng = token.value.toList();
        if (rng.size() != 2) return false;
        const int minVal = rng[0].toInt();
        const int maxVal = rng[1].toInt();
        const int p = task->priority();
        return p >= minVal && p <= maxVal;
    }

    case TokenType::Project: {
        const QString projectName = token.text;
        const auto *project = m_store->projectById(task->projectId());
        if (!project) return false;
        return project->name().compare(projectName, Qt::CaseInsensitive) == 0;
    }

    case TokenType::Label: {
        const QString labelName = token.text;
        return task->labels().contains(labelName, Qt::CaseInsensitive);
    }

    case TokenType::Section: {
        const QString sectionName = token.text;
        if (task->sectionId().isEmpty()) return false;
        const auto *section = m_store->sectionById(task->sectionId());
        if (!section) return false;
        return section->name().compare(sectionName, Qt::CaseInsensitive) == 0;
    }

    case TokenType::Search: {
        // Q2.26-B: Unicode-aware substring match via TextSearch. Default is
        // accent-INSENSITIVE so `search: cafe` finds tasks containing "café",
        // "Café", and "café" (NFD). Toggle via SearchAccentSensitive.
        const bool accentSensitive = m_store
            ? m_store->setting(QLatin1String(SettingsKeys::SearchAccentSensitive))
                      .toBool()
            : false;
        return TextSearch::contains(task->content(), token.text, !accentSensitive) ||
               TextSearch::contains(task->description(), token.text, !accentSensitive);
    }

    case TokenType::CreatedBefore: {
        // Task::createdAt stores UTC (see task.cpp ctor — currentDateTimeUtc).
        // Compare against the LOCAL date, otherwise the filter misclassifies
        // tasks created near midnight (e.g. 23:30 Minsk → UTC 20:30 prior day,
        // so `created today` would compare wrong-day to today's local date).
        const QDate createdDate = task->createdAt().toLocalTime().date();
        // B-4.39/40 FIX: also route created date tokens through DateParser.
        const QDate beforeDate = resolveDateToken(token.text);
        return createdDate.isValid() && beforeDate.isValid() && createdDate < beforeDate;
    }

    case TokenType::CreatedAfter: {
        // Task::createdAt stores UTC (see task.cpp ctor — currentDateTimeUtc).
        // Compare against the LOCAL date, otherwise the filter misclassifies
        // tasks created near midnight (e.g. 23:30 Minsk → UTC 20:30 prior day,
        // so `created today` would compare wrong-day to today's local date).
        const QDate createdDate = task->createdAt().toLocalTime().date();
        // B-4.39/40 FIX: also route created date tokens through DateParser.
        const QDate afterDate = resolveDateToken(token.text);
        return createdDate.isValid() && afterDate.isValid() && createdDate > afterDate;
    }

    case TokenType::CreatedToday: {
        // Task::createdAt stores UTC (see task.cpp ctor — currentDateTimeUtc).
        // Compare against the LOCAL date, otherwise the filter misclassifies
        // tasks created near midnight (e.g. 23:30 Minsk → UTC 20:30 prior day,
        // so `created today` would compare wrong-day to today's local date).
        const QDate createdDate = task->createdAt().toLocalTime().date();
        return createdDate.isValid() && createdDate == today;
    }

    case TokenType::CreatedThisWeek: {
        // "This week" = ISO week containing today (Mon..Sun).
        // Qt: QDate::dayOfWeek() returns 1=Mon..7=Sun. Compute week start
        // (Monday) and end (Sunday) and check inclusive containment.
        // Task::createdAt stores UTC (see task.cpp ctor — currentDateTimeUtc).
        // Compare against the LOCAL date, otherwise the filter misclassifies
        // tasks created near midnight (e.g. 23:30 Minsk → UTC 20:30 prior day,
        // so `created today` would compare wrong-day to today's local date).
        const QDate createdDate = task->createdAt().toLocalTime().date();
        if (!createdDate.isValid()) return false;
        const int daysFromMonday = today.dayOfWeek() - 1; // 0..6
        const QDate weekStart = today.addDays(-daysFromMonday);
        const QDate weekEnd   = weekStart.addDays(6);
        return createdDate >= weekStart && createdDate <= weekEnd;
    }

    case TokenType::Recurring:
        return task->dueIsRecurring();

    case TokenType::NotRecurring:
        // B-4.42 FIX: evaluate the !recurring token.
        return !task->dueIsRecurring();

    case TokenType::HasDeadline:
        // Matches tasks with any deadline date set.
        // Negation via "!deadline" is handled by AstNodeType::Not.
        return !task->deadline().isEmpty();

    case TokenType::Assigned:
        // F-4.43 FIX: stub for future multi-user support.
        // OpenTodo is currently single-user with no assignedTo field on Task.
        // "assigned: me" always matches (the current user owns all tasks).
        // "assigned: <other>" never matches (no other assignees exist).
        // TODO: when multi-user support is added, compare token.text against
        //       task->assignedTo() and the current user's identifier.
        return token.text.isEmpty() || token.text == QStringLiteral("me");

    case TokenType::Completed:
        return task->isCompleted();

    case TokenType::HasDue: {
        // value < 0 → "any due date"; value >= 0 → within today..today+N inclusive.
        const QDate due = effectiveDueDate(task);
        if (!due.isValid()) return false;
        const int n = token.value.toInt();
        if (n < 0) return true;
        const QDate end = today.addDays(n);
        // Inclusive of today and today+N (OpenTodo semantics).
        return due >= today && due <= end;
    }

    case TokenType::Subtask:
        return !task->parentId().isEmpty();

    case TokenType::ViewAll:
        // Always matches — used to override implicit project scoping.
        // Soft-deleted tasks are filtered out earlier by allTasks().
        return true;

    case TokenType::NoLabels:
        return task->labels().isEmpty();

    case TokenType::Identifier: {
        // Q2.26-B: Identifier predicates (bare words in the filter DSL) fall
        // through to content substring match — route through TextSearch so
        // they get the same NFC/NFD + accent folding as the explicit
        // `search:` predicate.
        const bool accentSensitive = m_store
            ? m_store->setting(QLatin1String(SettingsKeys::SearchAccentSensitive))
                      .toBool()
            : false;
        return TextSearch::contains(task->content(), token.text, !accentSensitive);
    }

    default:
        return false;
    }
}
