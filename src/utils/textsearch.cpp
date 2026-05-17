#include "textsearch.h"

#include <QChar>

namespace TextSearch {

QString normalize(const QString &s, bool stripDiacritics)
{
    if (s.isEmpty()) return QString();

    // Step 1: NFKC. This canonicalises ligatures (ﬁ → fi, ﬀ → ff), composes
    // combining sequences (e + ◌́ → é, U+0301 absorbed into U+00E9), and
    // collapses compatibility variants (full-width Latin → ASCII, Roman
    // numeral Ⅳ → IV, etc.).
    QString nfkc = s.normalized(QString::NormalizationForm_KC);

    QString folded;
    if (stripDiacritics) {
        // Step 2: re-decompose to NFD so combining marks become individually
        // addressable code points, then skip every Mark_Nonspacing (Mn).
        //
        // After NFKC we KNOW any decomposable composition has been put back
        // together; NFD undoes that one more time so we can drop the marks
        // we don't want to match on. Net effect:
        //   "café" (NFC)  → e + acute → e
        //   "ё"            → е + diaeresis → е
        //   "ñ"            → n + tilde → n
        //   "ﬁ"            → "fi" (from NFKC) → "fi" (no marks to drop)
        const QString nfd = nfkc.normalized(QString::NormalizationForm_D);
        folded.reserve(nfd.size());

        // Walk the QString as UTF-16 code units, but check QChar::category()
        // per code point. Surrogate pairs in Mn would be unusual (most
        // combining marks live in the BMP) but for correctness we look up
        // category via the high-level QChar(uint ucs4) overload after
        // assembling the code point.
        for (int i = 0; i < nfd.size(); ++i) {
            const QChar c = nfd.at(i);
            if (c.isHighSurrogate() && i + 1 < nfd.size()
                && nfd.at(i + 1).isLowSurrogate()) {
                const uint ucs4 = QChar::surrogateToUcs4(c, nfd.at(i + 1));
                if (QChar::category(ucs4) != QChar::Mark_NonSpacing) {
                    folded.append(c);
                    folded.append(nfd.at(i + 1));
                }
                ++i; // skip the low surrogate, already handled
            } else {
                if (c.category() != QChar::Mark_NonSpacing)
                    folded.append(c);
            }
        }
    } else {
        folded = std::move(nfkc);
    }

    // Step 3: case-fold (richer than toLower for some scripts; identity
    // for plain Latin/Cyrillic in practice but cheap and consistent).
    return folded.toCaseFolded();
}

bool contains(const QString &haystack,
              const QString &needle,
              bool stripDiacritics)
{
    if (needle.isEmpty()) return true; // QString::contains semantics
    return normalize(haystack, stripDiacritics)
        .contains(normalize(needle, stripDiacritics));
}

bool containsNormalized(const QString &haystack,
                        const QString &normalizedNeedle,
                        bool stripDiacritics)
{
    if (normalizedNeedle.isEmpty()) return true;
    return normalize(haystack, stripDiacritics).contains(normalizedNeedle);
}

} // namespace TextSearch
