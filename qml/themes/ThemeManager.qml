pragma Singleton
import QtQuick

QtObject {
    id: themeManager

    // The current theme name. Bound from Main.qml to settingsViewModel.theme
    // (singletons don't have access to context properties — Main wires the link).
    // Special value "system" follows Windows / OS color scheme; everything else
    // is one of the named palettes below.
    property string currentTheme: "system"

    // Custom accent color override. When non-empty (and a valid "#RRGGBB" string),
    // overrides the active palette's accentColor across the whole app.
    //
    // Persistence intentionally lives OUTSIDE this singleton — Qt.labs.settings
    // can't be imported here (Qt resolves singleton imports very early in engine
    // startup, before optional QML modules are mounted, which produced
    // "module Qt.labs.settings is not installed" failures cascading through
    // every consumer of this file). SettingsSectionAppearance writes the chosen
    // color into the QSettings store AND mirrors it onto
    // ThemeManager.customAccentHex at startup + on change.
    property string customAccentHex: ""

    // Apply a custom accent color override. Accepts only "#RRGGBB" hex strings;
    // returns true on success, false on validation failure.
    function setCustomAccent(hex) {
        if (typeof hex !== "string")
            return false
        var trimmed = hex.trim()
        if (!/^#[0-9A-Fa-f]{6}$/.test(trimmed))
            return false
        customAccentHex = trimmed
        return true
    }

    // Clear the custom accent override — palettes return to their native
    // accentColor immediately.
    function resetCustomAccent() {
        customAccentHex = ""
    }

    // ─── Day-rollover broadcaster (BUG-FIX C-6) ────────────────────────────
    //
    // Single shared "today" string in "yyyy-MM-dd". Computed once per minute
    // by `dayRolloverTimer`. Every consumer (TaskItem._isOverdue, ._today,
    // etc.) binds to this property instead of calling `new Date()` itself,
    // so when local midnight passes the binding chain re-evaluates exactly
    // once and overdue/today/upcoming colour cues update without requiring
    // the user to scroll, re-open the view, or restart the app.
    //
    // Storing the value here (in the singleton) avoids per-delegate timers
    // — important because there can be hundreds of TaskItems alive in a
    // single list view.
    property string todayStr: Qt.formatDate(new Date(), "yyyy-MM-dd")

    property Timer dayRolloverTimer: Timer {
        interval: 60000    // check once a minute — cheap, and worst-case
                           // staleness is ≤ 60s after midnight
        repeat:   true
        running:  true
        onTriggered: {
            var s = Qt.formatDate(new Date(), "yyyy-MM-dd")
            if (s !== themeManager.todayStr)
                themeManager.todayStr = s
        }
    }

    // All concrete palettes available to the user.
    readonly property var themes: ({
        "todoist_light": todoist_light,
        "dark":          dark,
        "kale":          kale,
        "blueberry":     blueberry,
        "lavender":      lavender,
        "raspberry":     raspberry,
        "graphite":      graphite,
        "sunflower":     sunflower,
        "highcontrast":  highcontrast
    })

    // What the OS is currently asking for. Reactive — Qt re-evaluates this
    // whenever the user toggles Windows light/dark in settings.
    // Application.styleHints is the QML-canonical way; Qt.styleHints is not
    // a standard binding source.
    readonly property bool systemPrefersDark: Application.styleHints.colorScheme === Qt.Dark

    // Resolves "system" → either todoist_light or dark; everything else is
    // a literal palette key.
    readonly property string effectiveThemeName:
        currentTheme === "system"
            ? (systemPrefersDark ? "dark" : "todoist_light")
            : currentTheme

    // Active theme colors — these are what QML components should bind to.
    //
    // Canonical palette property set (every palette below MUST define all of these):
    //   Structural:       isDark, backgroundColor, surfaceColor, cardColor
    //   Sidebar:          sidebarColor, sidebarTextColor, sidebarActiveColor
    //   Text:             textColor, secondaryTextColor
    //   Interactive:      accentColor, accentForeground, hoverColor, selectionColor
    //   Borders/overlay:  borderColor, scrimColor
    //   Semantic:         errorColor, warningColor, successColor, infoColor
    //   Priority:         priorityP1Color, priorityP2Color, priorityP3Color, priorityP4Color
    readonly property color backgroundColor:    activeTheme.backgroundColor
    readonly property color surfaceColor:       activeTheme.surfaceColor
    readonly property color sidebarColor:       activeTheme.sidebarColor
    readonly property color sidebarTextColor:   activeTheme.sidebarTextColor
    readonly property color sidebarActiveColor: activeTheme.sidebarActiveColor
    readonly property color textColor:          activeTheme.textColor
    readonly property color secondaryTextColor: activeTheme.secondaryTextColor
    // accentColor: custom override wins when set to a valid "#RRGGBB", else
    // falls back to the active palette's accent. customAccentHex is mirrored
    // from Qt.labs.settings so this binding re-evaluates whenever the user
    // applies or resets the override.
    readonly property color accentColor:
        (customAccentHex.length === 7 ? customAccentHex : activeTheme.accentColor)
    readonly property color accentForeground:      activeTheme.accentForeground
    readonly property color borderColor:        activeTheme.borderColor
    readonly property color cardColor:          activeTheme.cardColor
    readonly property color hoverColor:         activeTheme.hoverColor
    readonly property color selectionColor:     activeTheme.selectionColor
    readonly property color scrimColor:         activeTheme.scrimColor
    readonly property color errorColor:         activeTheme.errorColor
    readonly property color warningColor:       activeTheme.warningColor
    readonly property color successColor:       activeTheme.successColor
    readonly property color infoColor:          activeTheme.infoColor
    readonly property color priorityP1Color:    activeTheme.priorityP1Color
    readonly property color priorityP2Color:    activeTheme.priorityP2Color
    readonly property color priorityP3Color:    activeTheme.priorityP3Color
    readonly property color priorityP4Color:    activeTheme.priorityP4Color
    readonly property bool  isDark:             activeTheme.isDark

    // Resolve the active theme object via effectiveThemeName.
    readonly property QtObject activeTheme: themes[effectiveThemeName] || todoist_light

    // List of theme names for settings UI. Derived from palette keys so it
    // stays in sync automatically. "system" is prepended as a special first
    // card that mirrors the OS preference.
    readonly property var themeNames: ["system"].concat(Object.keys(themes))

    // ─────────────────────────────────────────────────────────────────────────
    // Individual theme definitions
    //
    // NOTE: Standalone palette files (TodoistLight.qml, TodoistDark.qml, etc.)
    // were deleted in the F-8.1 / B-8.1 cleanup — they were unreferenced dead
    // code. All palette data lives here.
    // ─────────────────────────────────────────────────────────────────────────

    readonly property QtObject todoist_light: QtObject {
        readonly property bool  isDark:             false
        readonly property color backgroundColor:    "#fafafa"
        readonly property color surfaceColor:       "#ffffff"
        // F-8.50: cardColor slightly off-white so cards are visible against surface
        readonly property color cardColor:          "#fcfcfc"
        readonly property color sidebarColor:       "#ffffff"
        readonly property color sidebarTextColor:   "#333333"
        readonly property color sidebarActiveColor: "#ffede8"
        readonly property color textColor:          "#202020"
        readonly property color secondaryTextColor: "#666666"
        readonly property color accentColor:        "#db4c3f"
        // F-8.45: white readable on this deep-red accent
        readonly property color accentForeground:      "#ffffff"
        readonly property color borderColor:        "#f0f0f0"
        readonly property color hoverColor:         "#f5f5f5"
        // F-8.4: ~20% alpha tint of accentColor (#db4c3f) on white background
        readonly property color selectionColor:     Qt.rgba(0.859, 0.298, 0.247, 0.18)
        // F-8.32: lighter scrim for light theme
        readonly property color scrimColor:         Qt.rgba(0, 0, 0, 0.4)
        // F-8.2: semantic colors for light theme — saturated, readable on white
        readonly property color errorColor:         "#d33b2c"
        readonly property color warningColor:       "#e08e10"
        readonly property color successColor:       "#3aa84e"
        readonly property color infoColor:          "#2980b9"
        // F-8.3: priority colors — deep, saturated for light backgrounds
        readonly property color priorityP1Color:    "#db4c3f"
        readonly property color priorityP2Color:    "#e08410"
        readonly property color priorityP3Color:    "#2070d8"
        readonly property color priorityP4Color:    "#aaaaaa"
    }

    readonly property QtObject dark: QtObject {
        readonly property bool  isDark:             true
        readonly property color backgroundColor:    "#1f1f1f"
        readonly property color surfaceColor:       "#282828"
        readonly property color cardColor:          "#303030"
        readonly property color sidebarColor:       "#282828"
        readonly property color sidebarTextColor:   "#e0e0e0"
        readonly property color sidebarActiveColor: "#3d2020"
        readonly property color textColor:          "#e0e0e0"
        readonly property color secondaryTextColor: "#999999"
        readonly property color accentColor:        "#db4c3f"
        // F-8.45: white readable on dark-red accent
        readonly property color accentForeground:      "#ffffff"
        readonly property color borderColor:        "#363636"
        readonly property color hoverColor:         "#353535"
        // F-8.4: ~20% alpha tint of accentColor (#db4c3f) on dark background
        readonly property color selectionColor:     Qt.rgba(0.859, 0.298, 0.247, 0.22)
        // F-8.32: standard dark scrim
        readonly property color scrimColor:         Qt.rgba(0, 0, 0, 0.5)
        // F-8.2: semantic colors — bright enough to pop on dark bg
        readonly property color errorColor:         "#e05050"
        readonly property color warningColor:       "#f49c18"
        readonly property color successColor:       "#4caf50"
        readonly property color infoColor:          "#3a96dd"
        // F-8.3: vivid priority colors for dark bg
        readonly property color priorityP1Color:    "#e05050"
        readonly property color priorityP2Color:    "#eb8909"
        readonly property color priorityP3Color:    "#246fe0"
        readonly property color priorityP4Color:    "#808080"
    }

    readonly property QtObject kale: QtObject {
        readonly property bool  isDark:             true
        readonly property color backgroundColor:    "#3b4a3e"
        readonly property color surfaceColor:       "#344438"
        readonly property color cardColor:          "#3a4f3d"
        readonly property color sidebarColor:       "#2d3930"
        readonly property color sidebarTextColor:   "#c8d8c6"
        // B-8.47: sidebarActiveColor 10-15% lighter than borderColor (#4a5e4d)
        readonly property color sidebarActiveColor: "#5a6f5d"
        readonly property color textColor:          "#d8e8d6"
        readonly property color secondaryTextColor: "#8ea88c"
        readonly property color accentColor:        "#7fba7a"
        // F-8.45: white readable on green accent
        readonly property color accentForeground:      "#ffffff"
        readonly property color borderColor:        "#4a5e4d"
        readonly property color hoverColor:         "#435643"
        // F-8.4: ~20% alpha tint of accentColor (#7fba7a) on dark-green bg
        readonly property color selectionColor:     Qt.rgba(0.498, 0.729, 0.478, 0.22)
        // F-8.32: standard dark scrim
        readonly property color scrimColor:         Qt.rgba(0, 0, 0, 0.5)
        // F-8.2: semantic colors tuned for green-tinted dark background
        readonly property color errorColor:         "#e05050"
        readonly property color warningColor:       "#f49c18"
        readonly property color successColor:       "#4caf50"
        readonly property color infoColor:          "#3a96dd"
        // F-8.3: priority colors on dark green background
        readonly property color priorityP1Color:    "#e05050"
        readonly property color priorityP2Color:    "#eb8909"
        readonly property color priorityP3Color:    "#246fe0"
        readonly property color priorityP4Color:    "#8ea88c"
    }

    readonly property QtObject blueberry: QtObject {
        readonly property bool  isDark:             true
        readonly property color backgroundColor:    "#1a1d3b"
        readonly property color surfaceColor:       "#1e2145"
        readonly property color cardColor:          "#222550"
        readonly property color sidebarColor:       "#151836"
        readonly property color sidebarTextColor:   "#b8bce8"
        // B-8.48: sidebarActiveColor 10-15% lighter than borderColor (#2a2e5e)
        readonly property color sidebarActiveColor: "#3a3e7e"
        readonly property color textColor:          "#d0d4f0"
        // R-8.54: bumped for better contrast (was #7a80b8)
        readonly property color secondaryTextColor: "#8a90c8"
        readonly property color accentColor:        "#6c88f4"
        // F-8.45: white readable on blue accent
        readonly property color accentForeground:      "#ffffff"
        readonly property color borderColor:        "#2a2e5e"
        readonly property color hoverColor:         "#282b58"
        // F-8.4: ~20% alpha tint of accentColor (#6c88f4) on dark-blue bg
        readonly property color selectionColor:     Qt.rgba(0.424, 0.533, 0.957, 0.22)
        // F-8.32: standard dark scrim
        readonly property color scrimColor:         Qt.rgba(0, 0, 0, 0.5)
        // F-8.2: semantic colors on deep blue background
        readonly property color errorColor:         "#e05050"
        readonly property color warningColor:       "#f49c18"
        readonly property color successColor:       "#4caf50"
        readonly property color infoColor:          "#3a96dd"
        // F-8.3: priority colors on dark blue background
        readonly property color priorityP1Color:    "#e05050"
        readonly property color priorityP2Color:    "#eb8909"
        readonly property color priorityP3Color:    "#5a8aff"
        readonly property color priorityP4Color:    "#8a90c8"
    }

    readonly property QtObject lavender: QtObject {
        readonly property bool  isDark:             true
        readonly property color backgroundColor:    "#2d2042"
        readonly property color surfaceColor:       "#332648"
        readonly property color cardColor:          "#352a4e"
        readonly property color sidebarColor:       "#251a38"
        readonly property color sidebarTextColor:   "#c8b8d8"
        // B-8.49: sidebarActiveColor 10-15% lighter than borderColor (#3e2e58)
        readonly property color sidebarActiveColor: "#4e3e68"
        readonly property color textColor:          "#dcd0e8"
        // R-8.53: bumped for better contrast (was #9080a8)
        readonly property color secondaryTextColor: "#a090b8"
        readonly property color accentColor:        "#b07fd0"
        // F-8.45: white readable on purple accent
        readonly property color accentForeground:      "#ffffff"
        readonly property color borderColor:        "#3e2e58"
        readonly property color hoverColor:         "#3a3055"
        // F-8.4: ~20% alpha tint of accentColor (#b07fd0) on dark-purple bg
        readonly property color selectionColor:     Qt.rgba(0.690, 0.498, 0.816, 0.22)
        // F-8.32: standard dark scrim
        readonly property color scrimColor:         Qt.rgba(0, 0, 0, 0.5)
        // F-8.2: semantic colors on purple-tinted dark background
        readonly property color errorColor:         "#e05050"
        readonly property color warningColor:       "#f49c18"
        readonly property color successColor:       "#4caf50"
        readonly property color infoColor:          "#3a96dd"
        // F-8.3: priority colors on dark purple background
        readonly property color priorityP1Color:    "#e05050"
        readonly property color priorityP2Color:    "#eb8909"
        readonly property color priorityP3Color:    "#246fe0"
        readonly property color priorityP4Color:    "#a090b8"
    }

    readonly property QtObject raspberry: QtObject {
        readonly property bool  isDark:             true
        readonly property color backgroundColor:    "#3b1a2d"
        readonly property color surfaceColor:       "#421e33"
        readonly property color cardColor:          "#481e38"
        readonly property color sidebarColor:       "#30152a"
        readonly property color sidebarTextColor:   "#d8b8c8"
        readonly property color sidebarActiveColor: "#582e48"
        readonly property color textColor:          "#e8d0dc"
        readonly property color secondaryTextColor: "#a88098"
        readonly property color accentColor:        "#e06088"
        // F-8.45: white readable on rose/pink accent
        readonly property color accentForeground:      "#ffffff"
        readonly property color borderColor:        "#502840"
        readonly property color hoverColor:         "#4e2540"
        // F-8.4: ~20% alpha tint of accentColor (#e06088) on dark-raspberry bg
        readonly property color selectionColor:     Qt.rgba(0.878, 0.376, 0.533, 0.22)
        // F-8.32: standard dark scrim
        readonly property color scrimColor:         Qt.rgba(0, 0, 0, 0.5)
        // F-8.2: semantic colors on raspberry-tinted dark background
        readonly property color errorColor:         "#e05050"
        readonly property color warningColor:       "#f49c18"
        readonly property color successColor:       "#4caf50"
        readonly property color infoColor:          "#3a96dd"
        // F-8.3: priority colors on dark raspberry background
        readonly property color priorityP1Color:    "#e05050"
        readonly property color priorityP2Color:    "#eb8909"
        readonly property color priorityP3Color:    "#246fe0"
        readonly property color priorityP4Color:    "#a88098"
    }

    readonly property QtObject graphite: QtObject {
        readonly property bool  isDark:             true
        readonly property color backgroundColor:    "#282828"
        readonly property color surfaceColor:       "#2e2e2e"
        readonly property color cardColor:          "#333333"
        readonly property color sidebarColor:       "#1f1f1f"
        readonly property color sidebarTextColor:   "#b0b0b0"
        readonly property color sidebarActiveColor: "#3a3a3a"
        readonly property color textColor:          "#d0d0d0"
        readonly property color secondaryTextColor: "#808080"
        // B-8.5: desaturated cool blue replaces #808080 so selection/hover/P3
        // are distinguishable from the pure-grayscale palette
        readonly property color accentColor:        "#7a9cc6"
        // F-8.45: white readable on desaturated blue accent
        readonly property color accentForeground:      "#ffffff"
        readonly property color borderColor:        "#3a3a3a"
        readonly property color hoverColor:         "#383838"
        // F-8.4: chromatic blue tint so selection stands out from grayscale;
        // using #3a5fa8 at ~20% alpha
        readonly property color selectionColor:     Qt.rgba(0.227, 0.373, 0.659, 0.20)
        // F-8.32: standard dark scrim
        readonly property color scrimColor:         Qt.rgba(0, 0, 0, 0.5)
        // F-8.2: semantic colors on neutral dark background
        readonly property color errorColor:         "#e05050"
        readonly property color warningColor:       "#f49c18"
        readonly property color successColor:       "#4caf50"
        readonly property color infoColor:          "#3a96dd"
        // F-8.3: Graphite is monochrome so we substitute graduated grays with a
        // hint of the chromatic blue accent for P3. P4 = secondaryTextColor.
        //   P1 = darkest (most urgent) warm-gray
        //   P2 = medium gray
        //   P3 = accent blue (cool distinguisher)
        //   P4 = secondary text gray
        readonly property color priorityP1Color:    "#c05050"
        readonly property color priorityP2Color:    "#a08060"
        readonly property color priorityP3Color:    "#7a9cc6"
        readonly property color priorityP4Color:    "#808080"
    }

    readonly property QtObject sunflower: QtObject {
        readonly property bool  isDark:             false
        readonly property color backgroundColor:    "#fffbf0"
        readonly property color surfaceColor:       "#ffffff"
        // F-8.51: warm-tinted card so cards are visible against white surface
        readonly property color cardColor:          "#fff8e8"
        readonly property color sidebarColor:       "#f5f0e0"
        readonly property color sidebarTextColor:   "#504020"
        readonly property color sidebarActiveColor: "#ffecc0"
        readonly property color textColor:          "#302010"
        readonly property color secondaryTextColor: "#806040"
        readonly property color accentColor:        "#e8a820"
        // F-8.45: golden accent fails WCAG with white — use near-black instead
        readonly property color accentForeground:      "#202020"
        readonly property color borderColor:        "#e8e0c8"
        readonly property color hoverColor:         "#f8f2e2"
        // F-8.4: ~18% alpha tint of accentColor (#e8a820) on warm-white background
        readonly property color selectionColor:     Qt.rgba(0.910, 0.659, 0.125, 0.18)
        // F-8.32: lighter scrim for light theme
        readonly property color scrimColor:         Qt.rgba(0, 0, 0, 0.4)
        // F-8.2: semantic colors for warm-light theme — saturated, readable on warm white
        readonly property color errorColor:         "#d33b2c"
        readonly property color warningColor:       "#e08e10"
        readonly property color successColor:       "#3aa84e"
        readonly property color infoColor:          "#2980b9"
        // F-8.3: priority colors — deep, saturated for warm-light backgrounds
        readonly property color priorityP1Color:    "#db4c3f"
        readonly property color priorityP2Color:    "#e08410"
        readonly property color priorityP3Color:    "#2070d8"
        readonly property color priorityP4Color:    "#aaaaaa"
    }

    // Высокая контрастность — accessibility palette for users with visual
    // impairments. Pure black/white/yellow/cyan with maximum-contrast borders.
    // Yellow accent on black background hits ~19:1 contrast ratio (WCAG AAA).
    readonly property QtObject highcontrast: QtObject {
        readonly property bool  isDark:             true
        readonly property color backgroundColor:    "#000000"
        readonly property color surfaceColor:       "#0D0D0D"
        // Near-black card, distinguishable from pure-black background by the
        // full-white border rather than fill brightness.
        readonly property color cardColor:          "#0D0D0D"
        readonly property color sidebarColor:       "#000000"
        readonly property color sidebarTextColor:   "#FFFFFF"
        // Bright-yellow active indicator — unmistakable against black sidebar.
        readonly property color sidebarActiveColor: "#1A1A1A"
        readonly property color textColor:          "#FFFFFF"
        // High-vis yellow for de-emphasized text. Still ~19:1 on black.
        readonly property color secondaryTextColor: "#FFFF00"
        readonly property color accentColor:        "#FFFF00"
        // Yellow accent demands black foreground — white-on-yellow fails WCAG.
        readonly property color accentForeground:   "#000000"
        // Full-white borders so every container edge is visible.
        readonly property color borderColor:        "#FFFFFF"
        readonly property color hoverColor:         "#1A1A1A"
        // Yellow selection tint at moderate alpha so text underneath stays
        // legible (#FFFF00 ≈ rgb(1,1,0)).
        readonly property color selectionColor:     Qt.rgba(1.0, 1.0, 0.0, 0.30)
        // Near-opaque scrim for modal dialogs against the pure-black background.
        readonly property color scrimColor:         Qt.rgba(0, 0, 0, 0.7)
        // Semantic colors — high-vis variants kept distinct from the yellow accent.
        readonly property color errorColor:         "#FF6060"
        readonly property color warningColor:       "#FFA000"
        readonly property color successColor:       "#00FF7F"
        readonly property color infoColor:          "#00BFFF"
        // Priority colors — saturated, easily distinguishable on black.
        readonly property color priorityP1Color:    "#FF4040"
        readonly property color priorityP2Color:    "#FFA000"
        readonly property color priorityP3Color:    "#00BFFF"
        readonly property color priorityP4Color:    "#FFFFFF"
    }
}
