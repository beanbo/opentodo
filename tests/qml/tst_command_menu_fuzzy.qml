import QtQuick
import QtTest

// Unit tests for the CommandMenu fuzzy-match algorithm.
// The function is copied verbatim from CommandMenu.qml so the tests stay
// dependency-free (no need to instantiate the full component).
TestCase {
    id: tc
    name: "FuzzyMatch"

    // ── Copy of CommandMenu.fuzzyMatch ────────────────────────────────────────
    function fuzzyMatch(query, target) {
        var q = query.toLowerCase()
        var t = target.toLowerCase()
        var qi = 0
        var score = 0
        for (var ti = 0; ti < t.length && qi < q.length; ti++) {
            if (t[ti] === q[qi]) {
                score += 1
                if (ti === 0 || t[ti - 1] === ' ') score += 2   // word-start bonus
                if (qi > 0) score += 1                           // consecutive bonus
                qi++
            }
        }
        return (qi === q.length) ? score : -1
    }

    // ── Helpers ───────────────────────────────────────────────────────────────
    function assertMatch(query, target) {
        var s = fuzzyMatch(query, target)
        verify(s >= 0,
               "Expected fuzzyMatch('" + query + "', '" + target + "') >= 0, got " + s)
    }

    function assertNoMatch(query, target) {
        var s = fuzzyMatch(query, target)
        compare(s, -1,
                "Expected fuzzyMatch('" + query + "', '" + target + "') == -1, got " + s)
    }

    // ── Tests ─────────────────────────────────────────────────────────────────

    function test_exact_match_returns_positive() {
        assertMatch("inbox", "inbox")
    }

    function test_substring_match_returns_positive() {
        assertMatch("box", "inbox")
    }

    function test_abbreviation_ai_matches_add_inbox() {
        // "ai" should match "Add Inbox" (a→A, i→I)
        assertMatch("ai", "Add Inbox")
    }

    function test_abbreviation_tt_matches_toggle_theme() {
        // "tt" should match "Toggle Theme"
        assertMatch("tt", "Toggle Theme")
    }

    function test_abbreviation_at_matches_add_task() {
        assertMatch("at", "Add task")
    }

    function test_case_insensitive_match() {
        // Query and target in same (Latin) alphabet — only case differs
        assertMatch("add",  "Add task")
        assertMatch("ADD",  "Add task")
        assertMatch("Task", "Add task")
        assertMatch("TASK", "Add task")
    }

    function test_no_match_returns_negative_one() {
        assertNoMatch("xyz", "inbox")
    }

    function test_empty_query_matches_everything() {
        // empty string — all chars matched trivially (qi stays 0, loop exits immediately)
        assertMatch("", "anything")
    }

    function test_query_longer_than_target_no_match() {
        assertNoMatch("abcdefghij", "abc")
    }

    function test_chars_must_appear_in_order() {
        // "ba" should NOT match "ab" (b comes after a in target but query needs b first)
        assertNoMatch("ba", "ab")
    }

    function test_word_start_bonus_increases_score() {
        // "t" at word start should score higher than mid-word match
        var startScore = fuzzyMatch("t", "Task")        // 't' at index 0 → +3
        var midScore   = fuzzyMatch("t", "at")          // 't' at index 1, no word-start
        verify(startScore > midScore,
               "Word-start match should score higher than mid-word match")
    }

    function test_consecutive_match_high_score() {
        // Matching consecutive chars should outscore scattered matches of same length
        var consecutiveScore = fuzzyMatch("add", "Add task")  // a,d,d consecutive at start
        var scatteredScore   = fuzzyMatch("add", "a long day") // a…d…d scattered
        verify(consecutiveScore > scatteredScore,
               "Consecutive match score " + consecutiveScore +
               " should exceed scattered score " + scatteredScore)
    }

    function test_score_positive_for_all_chars_matched() {
        var s = fuzzyMatch("sp", "Settings Page")
        verify(s > 0, "Both chars found → score should be positive, got " + s)
    }

    function test_single_char_query() {
        assertMatch("a", "a")
        assertMatch("a", "banana")
        assertNoMatch("z", "banana")
    }

    function test_full_label_match_scores_higher_than_abbreviation() {
        var fullScore  = fuzzyMatch("add task",  "Add task")
        var abbrScore  = fuzzyMatch("at",        "Add task")
        verify(fullScore > abbrScore,
               "Full label match should score higher than abbreviation")
    }
}
