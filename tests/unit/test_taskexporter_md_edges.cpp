// TaskExporter — Markdown-format edge-case pinning suite (Infra layer).
//
// Companion to test_taskexporter.cpp (basic CSV/MD/JSON happy-paths) and
// test_taskexporter_robustness.cpp (NUL bytes, deep parent chains, CRLF
// description, filename collision, RTL/bidi). This suite drills exclusively
// into the **Markdown output path** — exportToMarkdown / renderMdTaskRecursive
// in taskexporter.cpp — pinning behaviour around payloads that look
// suspiciously like Markdown syntax to a downstream CommonMark renderer.
//
// What's pinned:
//   - Inline emphasis sigils (`*`, `_`, triple backtick) in description: the
//     exporter does NOT escape; downstream renderer may interpret. We assert
//     the bytes survive verbatim and document the consumer-side risk.
//   - Link syntax `[text](url)` in title: pass-through verbatim.
//   - Brackets in title (`[draft]`, `[urgent]`): pass-through verbatim.
//   - HTML entities (`&amp;`, `&lt;`): NOT double-encoded — written as-is.
//   - 3-level nested subtask hierarchy: 0 / 2 / 4 / 6 space indents.
//   - 99 filename collisions in the same second: uniquifier appends
//     -1 / -2 / .../-99 and each file is preserved.
//   - Emoji in task title round-trips through MD UTF-8 byte sequence.
//
// Source file is kept NUL-free per the AUTOMOC requirement documented in
// test_taskexporter_robustness.cpp (a literal 0x00 byte in the TU prevents
// the .moc from being generated; we don't need NULs in this suite).
//
// IMPORTANT: NO PRODUCTION-CODE CHANGES. Every assertion pins current
// observed behaviour. When a future fix changes that behaviour, flipping
// the corresponding QCOMPARE/QVERIFY here is the regression-bench.

#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QStringList>
#include <QRegularExpression>
#include <QSettings>

#include "services/taskexporter.h"
#include "storage/datastore.h"
#include "model/task.h"
#include "model/project.h"

class TestTaskExporterMdEdges : public QObject
{
    Q_OBJECT

private:
    static QString readUtf8(const QString &path)
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return {};
        return QString::fromUtf8(f.readAll());
    }

    static QByteArray readBytes(const QString &path)
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return {};
        return f.readAll();
    }

private slots:
    void initTestCase()
    {
        // Unique org/app + QSettings namespace per suite — protects against
        // cross-suite pollution under `ctest -j` (test_taskexporter and
        // test_taskexporter_robustness use their own org/app, this one its own).
        QCoreApplication::setOrganizationName(QStringLiteral("OpenTodoTest_MdEdges"));
        QCoreApplication::setApplicationName(QStringLiteral("test_taskexporter_md_edges"));
        QStandardPaths::setTestModeEnabled(true);
    }

    void cleanupTestCase()
    {
        QStandardPaths::setTestModeEnabled(false);
    }

    // =====================================================================
    //  Slot 1 — Description containing a `*` adjacent to text
    //           ("see *important* note") emits the asterisks unescaped.
    //
    //  CommonMark would parse `*important*` as inline emphasis (italic).
    //  The exporter writes the description after a "  > " blockquote prefix
    //  with no escaping pass. We assert the asterisks land verbatim and
    //  document the renderer-side risk via qInfo.
    // =====================================================================

    void md_description_with_asterisk_does_not_break_bold_emphasis()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("Emph"));
        Task *t = ds.addTask(QStringLiteral("review"), p->id());
        // Both single-* (italic) and **bold** runs in the same description.
        const QString desc = QStringLiteral("see *important* and **double-bold** notes");
        t->setDescription(desc);

        TaskExporter exp(&ds);
        const QString path = exp.exportToMarkdown(tmp.path());
        QVERIFY(!path.isEmpty());

        const QString md = readUtf8(path);
        // The description is rendered after "  > " (blockquote continuation).
        // The asterisks survive byte-for-byte — the exporter never escapes.
        QVERIFY2(md.contains(QStringLiteral("  > see *important* and **double-bold** notes")),
                 "MD must emit description verbatim — exporter does NOT escape `*`");
        // Confirm the count of `*` survives (4 in source -> 4 in output line).
        const int asteriskCount = md.count(QChar('*'));
        QCOMPARE(asteriskCount, 6); // 2 singles + 4 doubles = 6 chars

        qInfo("Pinned: TaskExporter emits `*` runs verbatim in MD. A downstream "
              "CommonMark renderer will italicize *important* and bold "
              "**double-bold** — pretty when intentional, surprising if the "
              "user wrote `*` as a literal bullet/glyph. Decide later if "
              "MD-aware escaping (`\\*`) is desired.");
    }

    // =====================================================================
    //  Slot 2 — Description containing a `_` mid-word
    //           ("snake_case_name") does not auto-italicise.
    //
    //  CommonMark intra-word underscores are NOT emphasis (rule: `_` only
    //  triggers emphasis at word boundaries), so a renderer would leave
    //  `snake_case_name` alone. But: we exercise the exporter's NO-escape
    //  contract — the bytes pass through. A boundary case like ` _word_ ` IS
    //  emphasis under CommonMark — also pass-through.
    // =====================================================================

    void md_description_with_underscore_inside_word_does_not_italicise()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("Underscore"));
        Task *t = ds.addTask(QStringLiteral("refactor"), p->id());
        // Three patterns: intra-word (NOT emphasis), boundary (IS emphasis),
        // and double-underscore intra-word (the GitHub Flavored Markdown
        // contention zone where parsers differ).
        const QString desc = QStringLiteral(
            "rename snake_case_name to camelCase, mark _draft_ as __wip__ candidate");
        t->setDescription(desc);

        TaskExporter exp(&ds);
        const QString path = exp.exportToMarkdown(tmp.path());
        QVERIFY(!path.isEmpty());

        const QString md = readUtf8(path);
        // Whole description survives verbatim.
        QVERIFY2(md.contains(desc),
                 "MD must pass description through unmodified — no `_` escaping");
        // Underscore count: 2 (snake_case) + 2 (_draft_) + 4 (__wip__) = 8.
        QCOMPARE(md.count(QChar('_')), 8);

        qInfo("Pinned: intra-word `_` is safe with CommonMark (not emphasis) but "
              "boundary `_draft_` and `__wip__` ARE. The exporter passes all "
              "three through verbatim — escaping policy lives elsewhere.");
    }

    // =====================================================================
    //  Slot 3 — Description containing a triple-backtick run does not
    //           open a code block.
    //
    //  This is the most adversarial Markdown-injection vector: writing
    //  ```bash\nrm -rf /\n``` inside a description would, under a permissive
    //  renderer, render as a syntax-highlighted shell snippet. The exporter
    //  flattens [\r\n]+ runs to a single space, so the literal newlines never
    //  reach the output — the triple-backtick + content all land on the SAME
    //  blockquote line ("  > ```bash rm -rf / ```"), which is safer than the
    //  multi-line form but still a literal ``` run.
    // =====================================================================

    void md_description_with_triple_backtick_does_not_open_code_block()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("Code"));
        Task *t = ds.addTask(QStringLiteral("audit"), p->id());
        // CRLF between fence opener / payload / closer — exporter must
        // collapse the [\r\n]+ runs to single spaces (Q3.06 contract).
        const QString desc = QStringLiteral("```bash\nrm -rf /\n```");
        t->setDescription(desc);

        TaskExporter exp(&ds);
        const QString path = exp.exportToMarkdown(tmp.path());
        QVERIFY(!path.isEmpty());

        const QByteArray raw = readBytes(path);

        // The newlines inside the description must be collapsed to single
        // spaces by the [\r\n]+ regex in renderMdTaskRecursive — confirms
        // the "  > " prefix only appears ONCE for this description.
        const int bqCount = QString::fromUtf8(raw).count(QStringLiteral("  > "));
        QCOMPARE(bqCount, 1);

        // The full collapsed payload lands on one line after the "  > ".
        QVERIFY2(raw.contains("  > ```bash rm -rf / ```"),
                 "MD description must collapse \\n inside triple-backtick run to single spaces");

        // Backtick byte count survives (6 — three open + three close).
        QCOMPARE(QString::fromUtf8(raw).count(QChar('`')), 6);

        qInfo("Pinned: TaskExporter collapses newlines in description (Q3.06 "
              "[\\r\\n]+ regex), which neutralises the multi-line triple-backtick "
              "code-block opener. Single-line ``` runs still survive and a "
              "permissive renderer may render the rest of the line as inline code. "
              "If hardened later, write a sentinel like `&#96;` or wrap in HTML.");
    }

    // =====================================================================
    //  Slot 4 — Description with full `[text](https://url)` link syntax
    //           passes through verbatim — exporter does not rewrite it.
    //
    //  Renderers turn this into an `<a>` tag. Users may have written it as
    //  intentional Markdown (good) or as the literal characters (bad).
    //  Exporter has no way to know — pin as pass-through.
    // =====================================================================

    void md_description_with_link_syntax_passes_through()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("Link"));
        Task *t = ds.addTask(QStringLiteral("read"), p->id());
        // Both inline link AND auto-link forms.
        const QString desc = QStringLiteral(
            "see [docs](https://example.com/path?a=1&b=2) or https://raw.example.com");
        t->setDescription(desc);

        TaskExporter exp(&ds);
        const QString path = exp.exportToMarkdown(tmp.path());
        QVERIFY(!path.isEmpty());

        const QString md = readUtf8(path);
        // Exact byte-for-byte preservation of the link payload.
        QVERIFY2(md.contains(desc),
                 "MD must pass `[text](url)` link syntax through unmodified");

        // No part of the URL is URL-encoded by the exporter (`&` stays `&`,
        // `?` stays `?`, `=` stays `=` etc.) — pin the no-rewrite contract.
        QVERIFY(md.contains(QStringLiteral("?a=1&b=2")));
        QVERIFY(!md.contains(QStringLiteral("%3F"))); // would indicate URL-encoding
        QVERIFY(!md.contains(QStringLiteral("&amp;"))); // no HTML entity rewrite
    }

    // =====================================================================
    //  Slot 5 — Task TITLE containing brackets (`[draft]`, `[urgent]`)
    //           passes through verbatim into the checkbox line.
    //
    //  This is risky: the MD output line begins with `- [ ] ` (or `- [x] `)
    //  which is itself the GitHub task-list syntax. A task with title
    //  "[draft] foo" yields `- [ ] [draft] foo` — most renderers handle this
    //  fine (one checkbox + literal `[draft]`), but a buggy renderer might
    //  see a second checkbox. Pin the actual emitted bytes.
    // =====================================================================

    void md_task_title_with_brackets_passes_through()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("Brackets"));
        // Three variants: bare brackets, brackets-with-content, link-syntax
        // brackets, and `[x]`-shaped brackets that look like a CHECKED checkbox.
        ds.addTask(QStringLiteral("[draft] write doc"), p->id());
        ds.addTask(QStringLiteral("[urgent] fix bug"), p->id());
        ds.addTask(QStringLiteral("see [README] for setup"), p->id());
        ds.addTask(QStringLiteral("[x] this looks checked"), p->id());

        TaskExporter exp(&ds);
        const QString path = exp.exportToMarkdown(tmp.path());
        QVERIFY(!path.isEmpty());

        const QString md = readUtf8(path);
        // Each title emerges verbatim after the `- [ ] ` checkbox prefix.
        QVERIFY(md.contains(QStringLiteral("- [ ] [draft] write doc")));
        QVERIFY(md.contains(QStringLiteral("- [ ] [urgent] fix bug")));
        QVERIFY(md.contains(QStringLiteral("- [ ] see [README] for setup")));
        // The "looks-checked" task is OPEN (default priority 1, isCompleted=false),
        // so MD emits `- [ ] [x] this looks checked` — TWO sets of brackets
        // back-to-back. A naive task-list parser MIGHT mistakenly treat the
        // second `[x]` as the checkbox.
        QVERIFY2(md.contains(QStringLiteral("- [ ] [x] this looks checked")),
                 "Title-leading [x] must NOT be interpreted as the checkbox state");

        qInfo("Pinned: title brackets pass through verbatim. The `- [ ] [x] foo` "
              "shape is a known renderer-fragility — most tools handle it, but "
              "a strict GFM task-list parser may misread the second bracket pair.");
    }

    // =====================================================================
    //  Slot 6 — Description with HTML entities (`&amp;`, `&lt;`, `&#39;`)
    //           survives WITHOUT being decoded OR double-encoded.
    //
    //  Critical for round-trip safety: if a user pastes `&amp;` because their
    //  source data is HTML-encoded, we must NOT decode (would lose info), and
    //  we must NOT double-encode (e.g. emit `&amp;amp;`). Verify the raw
    //  bytes match exactly.
    // =====================================================================

    void md_task_with_html_entities_in_description_does_not_double_encode()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("Entities"));
        Task *t = ds.addTask(QStringLiteral("escape audit"), p->id());
        // Common HTML entities: ampersand-as-entity, less-than, apostrophe (numeric).
        const QString desc = QStringLiteral(
            "Q&amp;A: use &lt;b&gt;bold&lt;/b&gt; and don&#39;t forget &amp;copy;");
        t->setDescription(desc);

        TaskExporter exp(&ds);
        const QString path = exp.exportToMarkdown(tmp.path());
        QVERIFY(!path.isEmpty());

        const QString md = readUtf8(path);
        // Verbatim preservation — no decode (still &amp;), no double-encode
        // (no &amp;amp; chain).
        QVERIFY2(md.contains(desc),
                 "MD must preserve HTML entities byte-for-byte (no decode, no double-encode)");
        // No accidental promotion to literal characters.
        QVERIFY2(!md.contains(QStringLiteral("Q&A:")),
                 "&amp; must NOT have been decoded to literal &");
        // No double-encoding (the existing &amp; must not become &amp;amp;).
        QVERIFY2(!md.contains(QStringLiteral("&amp;amp;")),
                 "&amp; must NOT have been double-encoded");
        QVERIFY2(!md.contains(QStringLiteral("&amp;lt;")),
                 "&lt; must NOT have been double-encoded");
    }

    // =====================================================================
    //  Slot 7 — Three-level nested subtask hierarchy renders with correct
    //           2-space indents (depth 0 / 2 / 4 / 6).
    //
    //  Complements test_taskexporter_robustness slot 6 (4-level) by pinning
    //  the EXACT byte sequence at each level — including the description
    //  blockquote continuation that lives at `depth * 2 + 2` spaces.
    //  Also verifies the rendering order: each parent's subtask block
    //  appears IMMEDIATELY after the parent's `- [ ]` line, not interleaved
    //  with the next root task.
    // =====================================================================

    void md_nested_subtasks_3_levels_deep_indent_correctly()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("Tree"));
        // Two root tasks. Root-A has 3-level subtask chain, each with
        // description, so we exercise both the bullet line AND the
        // blockquote-continuation indent at every depth.
        Task *rootA = ds.addTask(QStringLiteral("rootA"), p->id());
        rootA->setDescription(QStringLiteral("rootA-desc"));
        Task *L1 = ds.addTask(QStringLiteral("L1-child"), p->id());
        L1->setParentId(rootA->id());
        L1->setDescription(QStringLiteral("L1-desc"));
        Task *L2 = ds.addTask(QStringLiteral("L2-grand"), p->id());
        L2->setParentId(L1->id());
        L2->setDescription(QStringLiteral("L2-desc"));
        Task *L3 = ds.addTask(QStringLiteral("L3-great"), p->id());
        L3->setParentId(L2->id());
        L3->setDescription(QStringLiteral("L3-desc"));
        // A separate root task to confirm subtasks are NOT promoted past it.
        ds.addTask(QStringLiteral("rootB"), p->id());

        TaskExporter exp(&ds);
        const QString path = exp.exportToMarkdown(tmp.path());
        QVERIFY(!path.isEmpty());

        const QString md = readUtf8(path);

        // Per-level checkbox indent — depth * 2 spaces.
        QVERIFY2(md.contains(QStringLiteral("- [ ] rootA")),
                 "depth 0 root must have NO leading indent");
        QVERIFY2(md.contains(QStringLiteral("  - [ ] L1-child")),
                 "depth 1 must have 2-space indent");
        QVERIFY2(md.contains(QStringLiteral("    - [ ] L2-grand")),
                 "depth 2 must have 4-space indent");
        QVERIFY2(md.contains(QStringLiteral("      - [ ] L3-great")),
                 "depth 3 must have 6-space indent");

        // Per-level description blockquote indent — depth * 2 + 2 spaces
        // (the `  > ` literal sits at "indent + two spaces" in the source).
        QVERIFY2(md.contains(QStringLiteral("  > rootA-desc")),
                 "depth 0 description must be at 2-space indent (  > )");
        QVERIFY2(md.contains(QStringLiteral("    > L1-desc")),
                 "depth 1 description must be at 4-space indent");
        QVERIFY2(md.contains(QStringLiteral("      > L2-desc")),
                 "depth 2 description must be at 6-space indent");
        QVERIFY2(md.contains(QStringLiteral("        > L3-desc")),
                 "depth 3 description must be at 8-space indent");

        // Rendering ORDER: rootA's subtask chain must appear BEFORE rootB,
        // not interleaved. Compare positions of the canonical L3-great
        // (deepest in chain) and rootB.
        const int posL3   = md.indexOf(QStringLiteral("L3-great"));
        const int posRootB = md.indexOf(QStringLiteral("- [ ] rootB"));
        QVERIFY(posL3 >= 0);
        QVERIFY(posRootB >= 0);
        QVERIFY2(posL3 < posRootB,
                 "rootA's subtask chain must fully complete before rootB renders");
    }

    // =====================================================================
    //  Slot 8 — Filename uniquifier handles 99 same-second collisions.
    //
    //  uniquifyPath() in taskexporter.cpp loops `for (int i = 1; i < 10000;
    //  ++i)` appending `-1`, `-2`, ..., `-9999`. Driving 99 back-to-back
    //  exports inside one test verifies the suffix path actually picks the
    //  next free slot each time (no overwrite). We don't push to 10000
    //  collisions — the loop has a pathological fall-through that overwrites,
    //  which is documented but not exercised here.
    // =====================================================================

    void md_filename_collision_uniquifier_handles_99_collisions()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("Many"));
        ds.addTask(QStringLiteral("only-task"), p->id());

        TaskExporter exp(&ds);

        // Collect 99 file paths from back-to-back exports. They MAY straddle
        // a wall-clock second boundary; if so, the timestamp itself differs
        // and the uniquifier never kicks in for those. Either way, ALL 99
        // paths must be unique (no silent overwrite).
        QSet<QString> seen;
        for (int i = 0; i < 99; ++i) {
            const QString path = exp.exportToMarkdown(tmp.path());
            QVERIFY2(!path.isEmpty(),
                     qPrintable(QStringLiteral("Export %1 returned empty path").arg(i)));
            QVERIFY2(QFile::exists(path),
                     qPrintable(QStringLiteral("Export %1 file missing on disk: %2").arg(i).arg(path)));
            QVERIFY2(!seen.contains(path),
                     qPrintable(QStringLiteral("Duplicate path on export %1: %2").arg(i).arg(path)));
            seen.insert(path);
        }
        QCOMPARE(seen.size(), 99);

        // Sweep the directory: exactly 99 .md files exist. (No stray suffix
        // beyond the 99 we asked for — pin the upper bound.)
        QDir d(tmp.path());
        const QStringList mdFiles = d.entryList(QStringList() << QStringLiteral("*.md"),
                                                QDir::Files);
        QCOMPARE(mdFiles.size(), 99);

        // The set of -N suffixes that actually appear: every file must either
        // be a bare timestamp (.md) or carry a -<digits>.md suffix. No other
        // pattern is allowed.
        static const QRegularExpression suffixRe(
            QStringLiteral("opentodo-export-\\d{8}-\\d{6}(-\\d+)?\\.md$"));
        for (const QString &name : mdFiles) {
            QVERIFY2(suffixRe.match(name).hasMatch(),
                     qPrintable(QStringLiteral("Unexpected MD filename shape: %1").arg(name)));
        }

        qInfo("Pinned: uniquifier successfully numbers up to 99 collisions in "
              "the same second. The loop bound is 10000 (then falls through "
              "to overwrite) — documented but not exercised here for runtime.");
    }

    // =====================================================================
    //  Slot 9 — Emoji in TASK TITLE round-trips through Markdown as the
    //           original UTF-8 byte sequence.
    //
    //  exportToMarkdown writes via QTextStream::setEncoding(Utf8). A 4-byte
    //  emoji (e.g. U+1F37A 🍺 = F0 9F 8D BA) and a 6-byte ZWJ sequence
    //  (👨‍💻 = F0 9F 91 A8 E2 80 8D F0 9F 92 BB) must both survive byte-equal.
    //  Also exercise emoji adjacent to the `- [ ]` prefix and adjacent to
    //  any priority/due flag the exporter appends.
    // =====================================================================

    void md_export_with_emoji_in_title_round_trips()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("Emoji"));
        // 1) BMP-range emoji (3 UTF-8 bytes).  ⭐ U+2B50 = E2 AD 90
        // 2) Supplementary plane (4 UTF-8 bytes).  🍺 U+1F37A = F0 9F 8D BA
        // 3) ZWJ sequence (multi-codepoint, 11 UTF-8 bytes total).
        //    👨‍💻 = U+1F468 ZWJ(U+200D) U+1F4BB = F0 9F 91 A8 E2 80 8D F0 9F 92 BB
        const QString starEmoji = QString::fromUtf8("\xE2\xAD\x90");
        const QString beerEmoji = QString::fromUtf8("\xF0\x9F\x8D\xBA");
        const QString devZwj    = QString::fromUtf8("\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x92\xBB");

        Task *t1 = ds.addTask(starEmoji + QStringLiteral(" priority pick"), p->id());
        Task *t2 = ds.addTask(QStringLiteral("brew ") + beerEmoji + QStringLiteral(" ASAP"), p->id());
        Task *t3 = ds.addTask(QStringLiteral("ship code ") + devZwj, p->id());
        // Give t1 a P3 priority + due date so emoji sits between content and
        // the appended flag glyphs (📅 / ⚑Pn) — pins the join behaviour.
        t1->setPriority(3);
        t1->setDueDate(QStringLiteral("2026-05-30"));
        Q_UNUSED(t2)
        Q_UNUSED(t3)

        TaskExporter exp(&ds);
        const QString path = exp.exportToMarkdown(tmp.path());
        QVERIFY(!path.isEmpty());

        // Read RAW bytes — UTF-8 sequence preservation is what we're after,
        // not QString round-tripping (QString could normalise silently).
        const QByteArray bytes = readBytes(path);

        // Each emoji's exact UTF-8 byte sequence is present.
        QVERIFY2(bytes.contains(QByteArray::fromHex("E2AD90")),
                 "Star emoji (E2 AD 90) must survive UTF-8 byte-for-byte");
        QVERIFY2(bytes.contains(QByteArray::fromHex("F09F8DBA")),
                 "Beer emoji (F0 9F 8D BA) must survive UTF-8 byte-for-byte");
        QVERIFY2(bytes.contains(QByteArray::fromHex("F09F91A8E2808DF09F92BB")),
                 "Developer ZWJ sequence must survive byte-for-byte (incl. ZWJ E2 80 8D)");

        // The exporter's OWN emoji glyphs (📅 = F0 9F 93 85, ⚑ = E2 9A 91)
        // appear when there's a due date or non-default priority. Both must
        // present for t1, which carries due + P3.
        QVERIFY2(bytes.contains(QByteArray::fromHex("F09F9385")),
                 "Exporter's 📅 (F0 9F 93 85) due-date glyph must appear");
        QVERIFY2(bytes.contains(QByteArray::fromHex("E29A91")),
                 "Exporter's ⚑ (E2 9A 91) priority glyph must appear");

        // Round-trip via QString equality on a per-task basis.
        const QString md = QString::fromUtf8(bytes);
        QVERIFY(md.contains(QStringLiteral("- [ ] ") + starEmoji + QStringLiteral(" priority pick")));
        QVERIFY(md.contains(QStringLiteral("- [ ] brew ") + beerEmoji + QStringLiteral(" ASAP")));
        QVERIFY(md.contains(QStringLiteral("- [ ] ship code ") + devZwj));
    }
};

QTEST_GUILESS_MAIN(TestTaskExporterMdEdges)
#include "test_taskexporter_md_edges.moc"
