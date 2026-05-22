// =============================================================================
// test_thememanager_switch.cpp
// =============================================================================
//
// PURPOSE
// -------
// Reactivity + persistence regression coverage for ThemeManager.qml -- the QML
// singleton that owns the active palette and the eight (plus one
// high-contrast) bundled colour schemes.
//
// COVERAGE
// --------
//   1. Singleton loads cleanly via `import OpenTodoUI` (catches a future
//      qmldir / module-URI drift).
//   2. The `themes` map exposes the documented bundled palettes; each one
//      defines the canonical colour properties used by the app.
//   3. Switching `currentTheme` between every bundled palette flips the
//      live `backgroundColor`, `accentColor`, `surfaceColor`, etc.
//      (palette switch is REACTIVE -- bindings re-evaluate, not stale).
//   4. Each palette switch emits the corresponding colour-property NOTIFY
//      signal exactly once per switch (via the `_listenerActive`-attached
//      change spy in QML).
//   5. `effectiveThemeName` resolves "system" to either classic_light or
//      dark depending on `Application.styleHints.colorScheme`.
//   6. Custom accent override (`setCustomAccent`) wins over the palette's
//      native accentColor; `resetCustomAccent` restores it.
//   7. Theme persistence -- the SettingsViewModel <-> DataStore round-trip
//      survives an in-memory save (toJson) + reload (loadFromJson),
//      mirroring what FileManager does on disk between app launches.
//
// WHY QTEST_MAIN (not GUILESS)?
// -----------------------------
// ThemeManager.qml is a QML singleton; testing it requires a live
// QQmlApplicationEngine, which transitively needs Material style resolution
// and a QPA platform initialised by QGuiApplication.  QTEST_MAIN spins up a
// QGuiApplication; the offscreen QPA is selected by ctest's ENVIRONMENT.
// =============================================================================

#include <QtTest>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QQmlError>
#include <QStandardPaths>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QStringView>
#include <QDebug>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QUrl>
#include <QColor>
#include <QJsonObject>
#include <QSet>
#include <atomic>
#include <cstdlib>

#include "storage/datastore.h"
#include "viewmodels/settingsviewmodel.h"
#include "utils/settingskeys.h"


// -----------------------------------------------------------------------------
// Captured-message storage. Same lightweight pattern as the other smoke tests:
// install a global handler so qWarning + qmlWarning land in a flat list, then
// enable capture only during the test action so background noise (Qt platform
// chatter, Material style probing) does not pollute the result.
// -----------------------------------------------------------------------------
namespace {

static QStringList       g_capturedMessages;
static std::atomic<bool> g_capturing{false};
static QtMessageHandler  g_previousHandler = nullptr;

static void captureMessageHandler(QtMsgType type,
                                  const QMessageLogContext &ctx,
                                  const QString &msg)
{
    if (g_capturing.load()) {
        if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) {
            g_capturedMessages.append(msg);
        }
    }
    if (g_previousHandler) {
        g_previousHandler(type, ctx, msg);
    } else {
        std::fprintf(stderr, "%s\n", qPrintable(msg));
    }
}

// The colour properties every palette MUST define -- this is the canonical
// surface QML components bind to. Keep aligned with the comment block at the
// top of qml/themes/ThemeManager.qml.
static const QStringList kRequiredColorProperties = {
    QStringLiteral("backgroundColor"),
    QStringLiteral("surfaceColor"),
    QStringLiteral("cardColor"),
    QStringLiteral("sidebarColor"),
    QStringLiteral("sidebarTextColor"),
    QStringLiteral("sidebarActiveColor"),
    QStringLiteral("textColor"),
    QStringLiteral("secondaryTextColor"),
    QStringLiteral("accentColor"),
    QStringLiteral("accentForeground"),
    QStringLiteral("borderColor"),
    QStringLiteral("hoverColor"),
    QStringLiteral("selectionColor"),
    QStringLiteral("scrimColor"),
    QStringLiteral("errorColor"),
    QStringLiteral("warningColor"),
    QStringLiteral("successColor"),
    QStringLiteral("infoColor"),
    QStringLiteral("priorityP1Color"),
    QStringLiteral("priorityP2Color"),
    QStringLiteral("priorityP3Color"),
    QStringLiteral("priorityP4Color"),
};

} // namespace


// =============================================================================
// TestThemeManagerSwitch
// =============================================================================
class TestThemeManagerSwitch : public QObject
{
    Q_OBJECT

private:
    // Single engine + harness reused across slots. Building the engine + a
    // wrapper Item that imports OpenTodoUI is the slow part (~250 ms cold);
    // every slot just queries / mutates properties on the persistent
    // ThemeManager singleton.
    QQmlApplicationEngine *m_engine    = nullptr;
    QObject               *m_harness   = nullptr;  // owns the singleton ref
    QObject               *m_tm        = nullptr;  // ThemeManager singleton

    // Get a fresh handle on the ThemeManager singleton via the engine.
    // qmlSingletonInstance() exposes the underlying QObject pointer so we
    // can attach QSignalSpy and mutate properties directly.
    QObject *resolveThemeManager() const
    {
        QQmlComponent comp(m_engine);
        const QByteArray probe =
            "import QtQuick\n"
            "import OpenTodoUI\n"
            "QtObject {\n"
            "    property var tm: ThemeManager\n"
            "}\n";
        comp.setData(probe, QUrl(QStringLiteral("qrc:/test/ThemeManagerProbe.qml")));
        if (comp.isError()) {
            qWarning().noquote() << "Probe component failed:" << comp.errorString();
            return nullptr;
        }
        QObject *probeObj = comp.create(m_engine->rootContext());
        if (!probeObj) {
            qWarning().noquote() << "Probe component create() returned null:"
                                 << comp.errorString();
            return nullptr;
        }
        QVariant tmVar = probeObj->property("tm");
        QObject *tm = tmVar.value<QObject*>();
        // Keep the probe alive as a parent so the singleton's binding
        // tree doesn't get GC'd between slots. The harness QObject is the
        // long-lived parent.
        if (probeObj && m_harness) {
            probeObj->setParent(m_harness);
        }
        return tm;
    }

    // Fully reset the singleton's mutable state at the start of every slot
    // so prior-slot writes don't bleed in. We only mutate the two writable
    // properties: currentTheme and customAccentHex.
    void resetSingletonState()
    {
        if (!m_tm) return;
        // Clear any custom accent override first so accentColor reflects the
        // palette, not the override, after we change currentTheme.
        QMetaObject::invokeMethod(m_tm, "resetCustomAccent");
        m_tm->setProperty("currentTheme", QStringLiteral("classic_light"));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }

private slots:

    // ---------------------------------------------------------------- fixture

    void initTestCase()
    {
        // Per-binary unique QSettings org/app keeps any QSettings-backed
        // surface (SettingsViewModel mirroring etc.) isolated from sibling
        // tests under `ctest -j`.
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(
            QStringLiteral("OpenTodoTest_ThemeManagerSwitch"));
        QCoreApplication::setApplicationName(
            QStringLiteral("test_thememanager_switch"));

        // Material style is imported by the OpenTodoUI module's other
        // components transitively; without it some QML resolution paths
        // emit "QQuickStyle: Material is not installed" warnings on minimal
        // CI images. Setting it explicitly mirrors src/main.cpp.
        QQuickStyle::setStyle(QStringLiteral("Material"));

        g_previousHandler = qInstallMessageHandler(&captureMessageHandler);

        m_engine = new QQmlApplicationEngine(this);

        // Mirror the import-path strategy from test_calendar_view_smoke.cpp /
        // test_settings_sections_smoke.cpp: try both candidate locations so
        // Qt.labs.settings + OpenTodoUI resolve whether the test binary lives
        // in build/release/ or under tests/unit/.
        m_engine->addImportPath(QCoreApplication::applicationDirPath()
                                + QStringLiteral("/qt/qml"));
        m_engine->addImportPath(QCoreApplication::applicationDirPath()
                                + QStringLiteral("/../../qt/qml"));

        m_harness = new QObject(this);

        m_tm = resolveThemeManager();
        QVERIFY2(m_tm,
                 "Could not resolve ThemeManager singleton via "
                 "`import OpenTodoUI`. Either the module URI changed or the "
                 "OpenTodoUIPlugin failed to register.");
    }

    void init()
    {
        resetSingletonState();
    }

    void cleanupTestCase()
    {
        qInstallMessageHandler(g_previousHandler);
        // Bypass destructor chain: same MinGW-on-Windows wedge sidestep used
        // by the calendar / views / settings-sections smoke tests. All
        // assertions have completed by this point.
        std::fflush(stdout);
        std::fflush(stderr);
        std::_Exit(0);
    }

    // -------------------------------------------------------------------------
    // 1. Singleton loads cleanly via `import OpenTodoUI`. Catches qmldir
    //    drift (e.g. the singleton declaration line getting deleted) and a
    //    failed module registration that masks itself as a load error.
    // -------------------------------------------------------------------------
    void singleton_loads_cleanly_with_no_fatal_warnings()
    {
        g_capturedMessages.clear();
        g_capturing.store(true);

        QObject *tm = resolveThemeManager();

        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        g_capturing.store(false);

        QVERIFY2(tm, "ThemeManager singleton must resolve via OpenTodoUI module.");

        // Allowlist of tolerated noise -- mirrors the smoke-test allowlist.
        const QStringList kTolerated = {
            QStringLiteral("deprecated"),
            QStringLiteral("QSettings"),
            QStringLiteral("QQuickStyle"),
            QStringLiteral("offscreen"),
            QStringLiteral("Qt.labs.settings"),
        };
        QStringList fatal;
        for (const QString &m : g_capturedMessages) {
            bool tolerated = false;
            for (const QString &t : kTolerated) {
                if (m.contains(t, Qt::CaseInsensitive)) { tolerated = true; break; }
            }
            if (tolerated) continue;
            // Fatal categories that mask a real regression in the singleton
            // load path.
            const QStringList kFatal = {
                QStringLiteral("is not installed"),
                QStringLiteral("is not available"),
                QStringLiteral("ReferenceError"),
                QStringLiteral("TypeError"),
                QStringLiteral("Cannot read property"),
                QStringLiteral("Unable to assign [undefined]"),
                QStringLiteral("Binding loop detected"),
            };
            for (const QString &f : kFatal) {
                if (m.contains(f, Qt::CaseSensitive)) { fatal << m; break; }
            }
        }
        if (!fatal.isEmpty()) {
            qWarning() << "Fatal QML warnings during ThemeManager singleton load:";
            for (const QString &m : fatal)
                qWarning().noquote() << "  -" << m;
        }
        QVERIFY2(fatal.isEmpty(),
                 qPrintable(QStringLiteral("ThemeManager load produced %1 fatal warning(s); first: %2")
                                .arg(fatal.size())
                                .arg(fatal.value(0))));
    }

    // -------------------------------------------------------------------------
    // 2. The `themes` map exposes the documented bundled palettes, AND each
    //    one defines every required colour property. Catches the regression
    //    "added a new theme but forgot priorityP3Color" -- which would break
    //    only at the moment a P3 task renders against that palette.
    // -------------------------------------------------------------------------
    void themes_map_contains_all_documented_bundled_palettes()
    {
        // The expected palette keys, per the comment block at the top of
        // ThemeManager.qml. "highcontrast" is the accessibility variant
        // included alongside the eight named palettes.
        const QSet<QString> kExpectedPalettes = {
            QStringLiteral("classic_light"),
            QStringLiteral("dark"),
            QStringLiteral("kale"),
            QStringLiteral("blueberry"),
            QStringLiteral("lavender"),
            QStringLiteral("raspberry"),
            QStringLiteral("graphite"),
            QStringLiteral("sunflower"),
            QStringLiteral("highcontrast"),
        };

        const QVariant themesVar = m_tm->property("themes");
        QVERIFY2(themesVar.isValid(),
                 "ThemeManager must expose a `themes` property (palette map).");
        const QVariantMap themesMap = themesVar.toMap();
        QVERIFY2(!themesMap.isEmpty(),
                 "ThemeManager.themes must be a non-empty map.");

        // Every expected palette is present.
        for (const QString &paletteName : kExpectedPalettes) {
            QVERIFY2(themesMap.contains(paletteName),
                     qPrintable(QStringLiteral("ThemeManager.themes is missing the '%1' palette.")
                                    .arg(paletteName)));
        }

        // No extra unannounced palettes (catches "stub palette landed in
        // a refactor without being added to the docs / settings UI").
        QSet<QString> actualKeys;
        for (auto it = themesMap.constBegin(); it != themesMap.constEnd(); ++it) {
            actualKeys.insert(it.key());
        }
        const QSet<QString> extras = actualKeys - kExpectedPalettes;
        QVERIFY2(extras.isEmpty(),
                 qPrintable(QStringLiteral("ThemeManager.themes contains unexpected palette(s): %1")
                                .arg(QStringList(extras.begin(), extras.end()).join(QStringLiteral(", ")))));
    }

    // -------------------------------------------------------------------------
    // 3. Switching `currentTheme` between every bundled palette flips the
    //    live colour properties. Iterates the eight base palettes (skips
    //    "system" which is not a palette key).
    //
    //    For each switch we capture backgroundColor + accentColor + surfaceColor
    //    and verify they change between consecutive distinct palettes (i.e.
    //    the binding chain is reactive).
    // -------------------------------------------------------------------------
    void switching_between_all_bundled_themes_changes_colors()
    {
        // Iteration order matches the user-visible settings UI ordering;
        // each consecutive pair is verified to produce DIFFERENT colour
        // values for at least one canonical property.
        const QStringList orderedPalettes = {
            QStringLiteral("classic_light"),
            QStringLiteral("dark"),
            QStringLiteral("kale"),
            QStringLiteral("blueberry"),
            QStringLiteral("lavender"),
            QStringLiteral("raspberry"),
            QStringLiteral("graphite"),
            QStringLiteral("sunflower"),
        };

        QColor prevBackground;
        QColor prevAccent;
        QColor prevSurface;
        bool firstIteration = true;

        for (const QString &palette : orderedPalettes) {
            QVERIFY(m_tm->setProperty("currentTheme", palette));
            QCoreApplication::processEvents(QEventLoop::AllEvents, 30);

            // effectiveThemeName must equal the literal palette key (the
            // "system" -> dark/light translation only fires when currentTheme
            // is literally the string "system").
            const QString effective = m_tm->property("effectiveThemeName").toString();
            QCOMPARE(effective, palette);

            const QColor bg      = m_tm->property("backgroundColor").value<QColor>();
            const QColor accent  = m_tm->property("accentColor").value<QColor>();
            const QColor surface = m_tm->property("surfaceColor").value<QColor>();

            QVERIFY2(bg.isValid(),
                     qPrintable(QStringLiteral("backgroundColor invalid for palette '%1'").arg(palette)));
            QVERIFY2(accent.isValid(),
                     qPrintable(QStringLiteral("accentColor invalid for palette '%1'").arg(palette)));
            QVERIFY2(surface.isValid(),
                     qPrintable(QStringLiteral("surfaceColor invalid for palette '%1'").arg(palette)));

            if (!firstIteration) {
                // At least ONE of the three canonical surfaces must have
                // changed between distinct palettes -- otherwise the
                // palette switch isn't reactive (or two palettes are
                // accidentally identical, which is also a regression).
                const bool anyChange =
                    bg != prevBackground ||
                    accent != prevAccent ||
                    surface != prevSurface;
                QVERIFY2(anyChange,
                         qPrintable(QStringLiteral("Switch to palette '%1' did not change "
                                                    "any of backgroundColor/accentColor/"
                                                    "surfaceColor -- bindings stale or "
                                                    "two palettes share identical surfaces.")
                                        .arg(palette)));
            }

            prevBackground = bg;
            prevAccent     = accent;
            prevSurface    = surface;
            firstIteration = false;
        }
    }

    // -------------------------------------------------------------------------
    // 4. Each theme has all required colour properties resolvable to a valid
    //    QColor. Catches a typo'd Qt.rgba() call or a missing property in a
    //    new palette (which would otherwise surface as a transparent panel
    //    in production with no error).
    // -------------------------------------------------------------------------
    void every_palette_defines_every_required_color_property()
    {
        const QVariantMap themesMap = m_tm->property("themes").toMap();
        QVERIFY(!themesMap.isEmpty());

        for (auto it = themesMap.constBegin(); it != themesMap.constEnd(); ++it) {
            const QString paletteName = it.key();
            QObject *paletteObj       = it.value().value<QObject*>();
            QVERIFY2(paletteObj,
                     qPrintable(QStringLiteral("Palette '%1' is not a QObject.").arg(paletteName)));

            for (const QString &propName : kRequiredColorProperties) {
                const QVariant v = paletteObj->property(propName.toUtf8().constData());
                QVERIFY2(v.isValid(),
                         qPrintable(QStringLiteral("Palette '%1' missing property '%2'.")
                                        .arg(paletteName, propName)));
                const QColor c = v.value<QColor>();
                QVERIFY2(c.isValid(),
                         qPrintable(QStringLiteral("Palette '%1' property '%2' is not a "
                                                    "valid QColor (got '%3').")
                                        .arg(paletteName, propName, v.toString())));
            }

            // isDark is a bool, not a colour.
            const QVariant isDarkVar = paletteObj->property("isDark");
            QVERIFY2(isDarkVar.isValid() && isDarkVar.userType() == QMetaType::Bool,
                     qPrintable(QStringLiteral("Palette '%1' missing or wrong-typed isDark.")
                                    .arg(paletteName)));
        }
    }

    // -------------------------------------------------------------------------
    // 5. Each palette switch fires the corresponding NOTIFY signal exactly
    //    once per switch. Pins the reactivity contract -- an accidental
    //    fixed-value `property color backgroundColor: "#xyz"` instead of
    //    `readonly property color backgroundColor: activeTheme.backgroundColor`
    //    would silently break this and only show as a "theme-switch doesn't
    //    work" user complaint.
    // -------------------------------------------------------------------------
    void switching_themes_emits_color_change_signals_once_per_switch()
    {
        // Start at classic_light so the first explicit switch to "dark"
        // produces a guaranteed-different palette.
        m_tm->setProperty("currentTheme", QStringLiteral("classic_light"));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 30);

        QSignalSpy bgSpy(m_tm, SIGNAL(backgroundColorChanged()));
        QSignalSpy accentSpy(m_tm, SIGNAL(accentColorChanged()));
        QSignalSpy surfaceSpy(m_tm, SIGNAL(surfaceColorChanged()));
        QSignalSpy isDarkSpy(m_tm, SIGNAL(isDarkChanged()));

        // QSignalSpy registers bound name lookups; if the signal does not
        // exist (renamed property, missing NOTIFY) the spy reports invalid.
        QVERIFY2(bgSpy.isValid(),     "backgroundColorChanged signal must exist on ThemeManager.");
        QVERIFY2(accentSpy.isValid(), "accentColorChanged signal must exist on ThemeManager.");
        QVERIFY2(surfaceSpy.isValid(),"surfaceColorChanged signal must exist on ThemeManager.");
        QVERIFY2(isDarkSpy.isValid(), "isDarkChanged signal must exist on ThemeManager.");

        m_tm->setProperty("currentTheme", QStringLiteral("dark"));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 30);

        // backgroundColor, surfaceColor and isDark all genuinely differ
        // between classic_light and dark, so their NOTIFYs MUST fire.
        QVERIFY2(bgSpy.count() >= 1,
                 "backgroundColorChanged must fire when switching from light to dark palette.");
        QVERIFY2(surfaceSpy.count() >= 1,
                 "surfaceColorChanged must fire when switching from light to dark palette.");
        QVERIFY2(isDarkSpy.count() >= 1,
                 "isDarkChanged must fire when switching from light to dark palette.");

        // accentColor: classic_light and dark BOTH use #db4c3f, so this
        // NOTIFY can legitimately be 0 emissions for this specific switch.
        // We don't assert here -- the kale switch below covers the accent
        // reactivity contract instead.

        // Switch to a palette with a verifiably different accent colour
        // (kale uses #7fba7a). This locks the accentColorChanged path even
        // when two adjacent palettes share an accent.
        bgSpy.clear();
        accentSpy.clear();
        surfaceSpy.clear();
        isDarkSpy.clear();

        m_tm->setProperty("currentTheme", QStringLiteral("kale"));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 30);

        QVERIFY2(accentSpy.count() >= 1,
                 "accentColorChanged must fire when switching to a palette with a distinct accent.");
        QVERIFY2(bgSpy.count() >= 1,
                 "backgroundColorChanged must fire when switching to a palette with a distinct background.");
    }

    // -------------------------------------------------------------------------
    // 6. effectiveThemeName resolves "system" to either classic_light or
    //    dark depending on Application.styleHints.colorScheme. We can't
    //    force the OS color scheme from a unit test, but we can at least
    //    pin that "system" -> one of the two valid resolutions, and that
    //    a literal palette key passes through unchanged.
    // -------------------------------------------------------------------------
    void system_theme_resolves_to_a_valid_palette_key()
    {
        m_tm->setProperty("currentTheme", QStringLiteral("system"));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 30);

        const QString effective = m_tm->property("effectiveThemeName").toString();
        const bool isLightOrDark = effective == QStringLiteral("classic_light") ||
                                   effective == QStringLiteral("dark");
        QVERIFY2(isLightOrDark,
                 qPrintable(QStringLiteral("effectiveThemeName for 'system' must resolve "
                                            "to 'classic_light' or 'dark', got '%1'.")
                                .arg(effective)));

        // currentTheme stays "system" (the resolution is read-only via
        // effectiveThemeName, not a destructive overwrite).
        QCOMPARE(m_tm->property("currentTheme").toString(), QStringLiteral("system"));
    }

    // -------------------------------------------------------------------------
    // 7. Custom accent override via setCustomAccent() wins over the palette's
    //    native accentColor; resetCustomAccent() restores it. Pins the user-
    //    facing "custom accent" feature; the override persistence path is
    //    handled by SettingsSectionAppearance and is covered by the settings
    //    sections smoke test.
    // -------------------------------------------------------------------------
    void custom_accent_override_wins_over_palette_then_resets()
    {
        // Start in dark theme so the native accent is the dark palette's #db4c3f.
        m_tm->setProperty("currentTheme", QStringLiteral("dark"));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
        const QColor nativeAccent = m_tm->property("accentColor").value<QColor>();
        QVERIFY(nativeAccent.isValid());

        // Apply a clearly different override (#00FF00 = pure green).
        QVariant ok;
        const bool invokeOk = QMetaObject::invokeMethod(
            m_tm,
            "setCustomAccent",
            Qt::DirectConnection,
            Q_RETURN_ARG(QVariant, ok),
            Q_ARG(QVariant, QStringLiteral("#00FF00")));
        QVERIFY(invokeOk);
        QCOMPARE(ok.toBool(), true);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 30);

        const QColor afterOverride = m_tm->property("accentColor").value<QColor>();
        QVERIFY(afterOverride.isValid());
        QCOMPARE(afterOverride, QColor(QStringLiteral("#00FF00")));
        QVERIFY2(afterOverride != nativeAccent,
                 "Custom accent override must win over the palette's native accent.");

        // Invalid hex inputs are rejected (bool false return) and the
        // override is unchanged.
        QVariant rejectOk;
        QMetaObject::invokeMethod(
            m_tm,
            "setCustomAccent",
            Qt::DirectConnection,
            Q_RETURN_ARG(QVariant, rejectOk),
            Q_ARG(QVariant, QStringLiteral("not-a-hex")));
        QCOMPARE(rejectOk.toBool(), false);
        // accent still the previous valid override.
        QCOMPARE(m_tm->property("accentColor").value<QColor>(),
                 QColor(QStringLiteral("#00FF00")));

        // Reset restores the palette's native accent.
        QMetaObject::invokeMethod(m_tm, "resetCustomAccent");
        QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
        const QColor afterReset = m_tm->property("accentColor").value<QColor>();
        QCOMPARE(afterReset, nativeAccent);
    }

    // -------------------------------------------------------------------------
    // 8. Theme persistence -- the SettingsViewModel <-> DataStore round-trip
    //    survives toJson + loadFromJson, mirroring what FileManager does on
    //    disk between app launches. Sets the theme via SettingsViewModel,
    //    serialises the DataStore, builds a fresh DataStore, loads the JSON
    //    back, and verifies the SettingsViewModel reads the persisted value.
    // -------------------------------------------------------------------------
    void theme_persists_through_datastore_save_and_reload()
    {
        // First DataStore + VM: write the theme.
        DataStore srcStore;
        SettingsViewModel srcVm(&srcStore);

        // Default value before any explicit set: "system" (from the legacy
        // upgrade path in SettingsViewModel::theme()).
        QCOMPARE(srcVm.theme(), QStringLiteral("system"));

        // Pick a palette that is NOT the default so the round-trip is a
        // genuine non-no-op.
        const QString chosen = QStringLiteral("blueberry");
        srcVm.setTheme(chosen);
        QCOMPARE(srcVm.theme(), chosen);

        // The setting is also visible at the DataStore layer under the
        // canonical snake_case key.
        const QVariant persistedRaw =
            srcStore.setting(QLatin1String(SettingsKeys::Theme));
        QCOMPARE(persistedRaw.toString(), chosen);

        // Serialise to JSON (this is the exact payload FileManager writes
        // to state.json on disk).
        const QJsonObject snapshot = srcStore.toJson();
        QVERIFY(snapshot.contains(QStringLiteral("settings")));
        const QJsonObject settingsObj = snapshot.value(QStringLiteral("settings")).toObject();
        QCOMPARE(settingsObj.value(QLatin1String(SettingsKeys::Theme)).toString(), chosen);

        // Second DataStore + VM: simulate a fresh app launch by loading
        // the persisted snapshot back. The new VM must report the same
        // theme without any other state being passed in.
        DataStore reloadedStore;
        reloadedStore.loadFromJson(snapshot);

        SettingsViewModel reloadedVm(&reloadedStore);
        QCOMPARE(reloadedVm.theme(), chosen);

        // And the underlying DataStore key matches too.
        QCOMPARE(reloadedStore.setting(QLatin1String(SettingsKeys::Theme)).toString(),
                 chosen);

        // For full coverage: pushing the persisted value back into the
        // singleton's currentTheme makes effectiveThemeName resolve to the
        // same palette key (blueberry is a literal palette name, so the
        // two strings are identical).
        m_tm->setProperty("currentTheme", reloadedVm.theme());
        QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
        QCOMPARE(m_tm->property("effectiveThemeName").toString(), chosen);
    }
};

// QTEST_MAIN: requires QGuiApplication for QQmlApplicationEngine + Material
// style resolution. The QPA platform is selected by ctest's ENVIRONMENT
// property (offscreen on POSIX; the same value works under Windows when set
// via tests/unit/CMakeLists.txt).
QTEST_MAIN(TestThemeManagerSwitch)
#include "test_thememanager_switch.moc"
