#pragma once

#include <QString>

/**
 * @brief Unicode-aware substring matching for OpenTodo's search code paths.
 *
 * Both SearchViewModel and the Filter DSL's `search:` predicate route through
 * this helper so that "café" finds "café" regardless of whether either side is
 * NFC-composed or NFD-decomposed, and so that — by default — accent variants
 * collapse together (ё → е, é → e, ñ → n).
 *
 * Normalization pipeline (applied to BOTH haystack and needle):
 *   1. Compatibility composition (NFKC) — composes characters and folds
 *      compatibility forms (ﬁ → fi, full-width A → A, U+00E9 / U+0065+U+0301
 *      become the same byte sequence).
 *   2. Optional combining-mark strip — decompose (NFD) and drop every code
 *      point in the Mark_Nonspacing category. This is what makes ё match е,
 *      é match e, etc.
 *   3. Case fold via QString::toCaseFolded() for case-insensitive matching.
 *
 * Performance: the input is walked at most twice (NFKC normalize + the
 * Mark_Nonspacing strip happens in a single decomposing pass). For 10 000
 * tasks this is O(total characters) which is well under a frame on any
 * desktop machine — the dominant cost is QString::contains, not the
 * normalization.
 *
 * Determinism: the function is pure — same input → same output. Callers
 * should normalize the query ONCE outside the inner per-task loop and pass
 * the pre-normalized form to repeated contains() calls when scanning a
 * large dataset.
 */
namespace TextSearch {

/**
 * @brief Returns the canonical form of @p s for substring search.
 *
 * Steps:
 *   - QString::normalized(NormalizationForm_KC) — NFKC.
 *   - If @p stripDiacritics is true, decompose (NFD) and skip every
 *     QChar whose category() is Mark_Nonspacing (Unicode "Mn").
 *   - toCaseFolded() — full Unicode case fold (better than toLower for
 *     diacritic-bearing letters like ß → ss in some forms).
 *
 * Empty input returns empty.
 */
QString normalize(const QString &s, bool stripDiacritics = true);

/**
 * @brief Substring containment test under normalize().
 *
 * Equivalent to:
 *   normalize(haystack, stripDiacritics).contains(normalize(needle, stripDiacritics))
 *
 * Empty needle returns true (matches QString::contains semantics).
 */
bool contains(const QString &haystack,
              const QString &needle,
              bool stripDiacritics = true);

/**
 * @brief Convenience: needle is already normalized — only haystack is
 *        normalized on each call.
 *
 * Hoist the needle normalization out of the inner loop when scanning
 * thousands of tasks.
 */
bool containsNormalized(const QString &haystack,
                        const QString &normalizedNeedle,
                        bool stripDiacritics = true);

} // namespace TextSearch
