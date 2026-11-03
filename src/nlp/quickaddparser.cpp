#include "quickaddparser.h"
#include "dateparser.h"

#include <QRegularExpression>
#include <QStringList>

QuickAddResult QuickAddParser::parse(const QString &input)
{
    QuickAddResult result;

    if (input.trimmed().isEmpty())
        return result;

    QString working = input;

    // ── 1. Extract #Project (first #token only) ───────────────────────────
    // Match #Word — single-word tokens (OpenTodo convention).
    // Unicode word characters are supported.
    {
        static const QRegularExpression projectRe(
            QStringLiteral("(?:^|\\s)#([^\\s#@!]+)"));
        const auto m = projectRe.match(working);
        if (m.hasMatch()) {
            result.projectName = m.captured(1);
            int removeStart = m.capturedStart(0);
            int removeLen   = m.capturedLength(0);
            if (working.at(removeStart) != QLatin1Char('#')) {
                removeStart += 1;
                removeLen   -= 1;
            }
            working.remove(removeStart, removeLen);
        }
    }

    // ── F-4.26 FIX: Extract subsequent #label tokens ─────────────────────
    // After the first #Project has been removed, any remaining #tokens are
    // treated as labels (OpenTodo convention for inbox-less projects).
    {
        static const QRegularExpression extraLabelRe(
            QStringLiteral("(?:^|\\s)#([^\\s#@!]+)"));
        struct Match { int start; int len; QString label; };
        QList<Match> matches;
        QRegularExpressionMatchIterator it = extraLabelRe.globalMatch(working);
        while (it.hasNext()) {
            auto m = it.next();
            int removeStart = m.capturedStart(0);
            int removeLen   = m.capturedLength(0);
            if (working.at(removeStart) != QLatin1Char('#')) {
                removeStart += 1;
                removeLen   -= 1;
            }
            matches.prepend(Match{removeStart, removeLen, m.captured(1)});
        }
        for (const auto &m : matches) {
            result.extraLabels.prepend(m.label);
            working.remove(m.start, m.len);
        }
    }

    // ── 2. Extract @label tokens (all occurrences) ────────────────────────
    {
        static const QRegularExpression labelRe(
            QStringLiteral("(?:^|\\s)@([^\\s@#!]+)"));
        QRegularExpressionMatchIterator it = labelRe.globalMatch(working);
        struct Match { int start; int len; QString label; };
        QList<Match> matches;
        while (it.hasNext()) {
            auto m = it.next();
            int removeStart = m.capturedStart(0);
            int removeLen   = m.capturedLength(0);
            if (working.at(removeStart) != QLatin1Char('@')) {
                removeStart += 1;
                removeLen   -= 1;
            }
            matches.prepend(Match{removeStart, removeLen, m.captured(1)});
        }
        for (const auto &m : matches) {
            result.labels.prepend(m.label);
            working.remove(m.start, m.len);
        }
    }

    // ── 3. Extract priority ───────────────────────────────────────────────
    {
        static const QRegularExpression prioRe(
            QStringLiteral("(?:^|(?<=\\s))p([1-4])(?=\\s|$)"),
            QRegularExpression::CaseInsensitiveOption);
        auto m = prioRe.match(working);
        if (m.hasMatch()) {
            result.priority = m.captured(1).toInt();
            working.remove(m.capturedStart(0), m.capturedLength(0));
        } else {
            static const QRegularExpression bangRe(
                QStringLiteral("(?:^|(?<=\\s))!([1-4])(?=\\s|$)"));
            auto bm = bangRe.match(working);
            if (bm.hasMatch()) {
                result.priority = bm.captured(1).toInt();
                working.remove(bm.capturedStart(0), bm.capturedLength(0));
            }
        }
    }

    working = working.simplified();

    // ── 4. Try to parse date from the remaining text ──────────────────────
    // Strategy (B-4.21 FIX):
    //   a) SUFFIX probe (original): try words[splitAt..end] as due candidate.
    //      content = words[0..splitAt-1].
    //   b) PREFIX probe (new):      try words[0..splitAt-1] as due candidate.
    //      content = words[splitAt..end].
    //   Pick whichever probe matches first (suffix has priority for ambiguous
    //   inputs where both could match — existing behaviour preserved).
    //   If both match at the same split index, suffix wins (original behaviour).

    if (!working.isEmpty()) {
        QStringList words = working.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        bool found = false;

        // Helper lambda: try a candidate string against both parsers.
        auto tryCandidate = [](const QString &candidate) -> bool {
            if (candidate.isEmpty()) return false;
            DateParseResult enRes = DateParser::parseEnglish(candidate, QDate::currentDate());
            if (enRes.valid) return true;
            DateParseResult ruRes = DateParser::parseRussian(candidate, QDate::currentDate());
            return ruRes.valid;
        };

        // We scan split positions 0..N-1 and record the LONGEST matching
        // suffix (first hit, smallest splitAt) and the LONGEST matching prefix
        // (last hit, largest splitAt), then pick whichever covers more words
        // as the date phrase.  Ties go to suffix.
        //
        // The prefix must be the longest one: the date rules only accept a
        // real date/time phrase (arbitrary trailing words are rejected), so
        // for "Tomorrow at 5pm call mom" both "Tomorrow" and "Tomorrow at 5pm"
        // match — stopping at the first (shortest) hit would leave "at 5pm"
        // in the title and drop the time.

        int suffixSplitAt = -1;
        int prefixSplitAt = -1;

        // A date phrase is a handful of words ("every 2 weeks at 9am",
        // "через 3 дня в 10:00"). Probing every split of a long paste re-parsed
        // O(N²) words (a 10 KB input took ~0.3-0.7 s), so only candidates of
        // up to kMaxDateWords words are tried. Inputs no longer than that
        // behave exactly as before.
        constexpr int kMaxDateWords = 32;
        const int wordCount = words.size();

        for (int splitAt = 0; splitAt < wordCount; ++splitAt) {
            // Suffix probe: dueCandidate = words[splitAt..]
            if (suffixSplitAt < 0 && wordCount - splitAt <= kMaxDateWords) {
                QString dueCandidate = QStringList(words.mid(splitAt)).join(QLatin1Char(' '));
                if (tryCandidate(dueCandidate))
                    suffixSplitAt = splitAt;
            }

            // Prefix probe (B-4.21): dueCandidate = words[0..splitAt]
            // (splitAt is inclusive for prefix, i.e. the prefix length is splitAt+1)
            if (splitAt < kMaxDateWords) {
                QString prefixCandidate = QStringList(words.mid(0, splitAt + 1)).join(QLatin1Char(' '));
                if (tryCandidate(prefixCandidate))
                    prefixSplitAt = splitAt; // keep scanning: longest prefix wins
            }

            // Suffix found and every prefix length already probed → done.
            if (suffixSplitAt >= 0 && splitAt + 1 >= kMaxDateWords)
                break;
        }

        if (suffixSplitAt >= 0 || prefixSplitAt >= 0) {
            found = true;
            // Choose the probe that covers more words in the date phrase.
            // Suffix covers (N - suffixSplitAt) words.
            // Prefix covers (prefixSplitAt + 1) words.
            // If both found, pick the one covering MORE words; ties → suffix.
            bool usePrefix = false;
            if (suffixSplitAt >= 0 && prefixSplitAt >= 0) {
                const int suffixDateWords = words.size() - suffixSplitAt;
                const int prefixDateWords = prefixSplitAt + 1;
                usePrefix = (prefixDateWords > suffixDateWords);
            } else if (prefixSplitAt >= 0) {
                usePrefix = true;
            }

            if (usePrefix) {
                result.dueString = QStringList(words.mid(0, prefixSplitAt + 1)).join(QLatin1Char(' '));
                result.content   = QStringList(words.mid(prefixSplitAt + 1)).join(QLatin1Char(' ')).simplified();
            } else {
                result.content   = QStringList(words.mid(0, suffixSplitAt)).join(QLatin1Char(' ')).simplified();
                result.dueString = QStringList(words.mid(suffixSplitAt)).join(QLatin1Char(' '));
            }
        }

        if (!found) {
            result.content   = working;
            result.dueString.clear();
        }
    }

    return result;
}
