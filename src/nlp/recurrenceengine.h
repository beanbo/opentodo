#pragma once

#include <QDate>
#include <QString>

/**
 * @brief Pure-domain helper for recurring task due-date advancement.
 *
 * Lives in the NLP layer because it shares vocabulary (English + Russian)
 * with DateParser (rules_en.cpp / rules_ru.cpp).
 */
class RecurrenceEngine
{
public:
    /**
     * @brief Compute the next due date from an RRULE string.
     *
     * B-4.60 FIX: Primary entry point now accepts an RFC-like RRULE string
     * (e.g. "FREQ=WEEKLY;BYDAY=MO,WE,FR") instead of a human-readable string.
     *
     * Recognised keys:
     *   FREQ        = DAILY | WEEKLY | MONTHLY | YEARLY
     *   INTERVAL    = <positive integer>  (default 1)
     *   BYDAY       = comma-separated two-letter day codes (MO TU WE TH FR SA SU)
     *   BYMONTHDAY  = <day-of-month 1-31>, or -1..-31 counted back from the month
 *                 end (MONTHLY only; -1 = last day of every month)
     *   COUNT       = <positive integer>  (RFC 5545 termination — see notes)
     *   UNTIL       = YYYYMMDD or YYYYMMDDTHHMMSSZ  (RFC 5545 termination)
     *   FROM        = COMPLETION  (schedule from completionDate rather than fromDate)
     *
     * Termination semantics (engine is stateless per call):
     *   * UNTIL: if the computed next occurrence is strictly after UNTIL, this
     *     function returns an invalid QDate.
     *   * COUNT=1: there is no "next" occurrence after the original — this
     *     function returns an invalid QDate. For COUNT>1 the engine cannot
     *     enforce termination on its own (it has no state); callers must track
     *     the running count externally if they need that.
     *
     * BYMONTHDAY semantics for short months (RFC 5545 §3.3.10):
     *   * MONTHLY;BYMONTHDAY=N where the target month has fewer than N days:
     *     skip ahead to the next month that has day N (do NOT clamp).
     *   * YEARLY;BYMONTHDAY=29 from Feb 29: skip non-leap years until a leap
     *     year is reached (do NOT clamp Feb 29 → Feb 28).
     *
     * @param recurrenceRule  RRULE string produced by DateParser.
     * @param fromDate        The current due date to advance from.
     * @param completionDate  Optional. Used when the rule contains ";FROM=COMPLETION".
     *                        If not supplied and FROM=COMPLETION, fromDate is used.
     * @return  Next occurrence, or an invalid QDate if the rule cannot be parsed,
     *          the FREQ is unrecognised, or termination (COUNT/UNTIL) is reached.
     */
    static QDate advance(const QString &recurrenceRule,
                         const QDate &fromDate,
                         const QDate &completionDate = QDate());

    /**
     * @brief Legacy fallback: advance from a human-readable string.
     *
     * Kept for tasks that were saved before the RRULE migration and therefore
     * have no recurrenceRule field but still have a humanString.
     *
     * @param humanString  e.g. "every day", "every Monday", "каждый день".
     * @param fromDate     The current due date.
     * @return  Next occurrence, or invalid QDate if unrecognised.
     */
    static QDate advanceFromHumanString(const QString &humanString, const QDate &fromDate);
};
