#pragma once

#include <QString>
#include <QStringList>

struct QuickAddResult {
    QString content;        // remaining text after extracting all tokens
    QString dueString;      // date/time phrase for DateParser
    int priority = 4;       // p1..p4 → 1..4 (default 4)
    QStringList labels;     // from @-tokens (and subsequent #label tokens per Todoist convention)
    QString projectName;    // from first #-token (name, not id)
    // F-4.26 FIX: additional #tags after the first are treated as labels (Todoist convention).
    QStringList extraLabels; // from second and subsequent #-tokens
};

class QuickAddParser
{
public:
    // Parse a free-form Todoist-style quick-add string.
    // Strategy:
    //   1. Extract #Project (first occurrence) → projectName
    //   2. Extract subsequent #label tokens → extraLabels  (F-4.26)
    //   3. Extract @label tokens (all occurrences) → labels
    //   4. Extract priority: p1/p2/p3/p4 (word-boundary) or !1/!2/!3/!4 prefix → priority
    //   5. Pass the remaining text to DateParser (Russian + English).
    //      Probe both SUFFIX (existing) and PREFIX (B-4.21) of each word-split position.
    //      Pick the probe that covers the most characters of the working string.
    static QuickAddResult parse(const QString &input);
};
