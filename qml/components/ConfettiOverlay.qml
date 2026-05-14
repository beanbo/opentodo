// ConfettiOverlay.qml
// Full-window confetti burst shown when the user hits their daily karma goal.
// Pure QML — no QtQuick.Particles dependency. 60 rectangles fall from the top
// with randomised positions, colours, rotation, and fall duration.
// Call celebrate() to trigger; the overlay hides itself after 3.5 s.

import QtQuick
import OpenTodoUI

Item {
    id: root
    anchors.fill: parent
    visible: false
    z: 9999

    // ── Public API ────────────────────────────────────────────────────────────
    function celebrate() {
        visible = true
        confettiRepeater.model = 60
        autoHide.restart()
    }

    // ── Palette: seven distinct hues that work on both light and dark themes ──
    readonly property var _palette: [
        "#db4c3f", "#f49c18", "#fad000", "#7ecc49",
        "#158fad", "#4073ff", "#884dff"
    ]

    // ── Confetti pieces ───────────────────────────────────────────────────────
    Repeater {
        id: confettiRepeater
        model: 0   // spawned to 60 in celebrate(), reset to 0 on hide

        delegate: Item {
            id: piece

            // Each piece captures its own random values at creation time so the
            // animation is deterministic for its lifetime even if the parent
            // width changes mid-fall.
            readonly property real _startX:    Math.random() * root.width
            readonly property real _wobble:    (Math.random() - 0.5) * 180
            readonly property real _startRot:  Math.random() * 360
            readonly property real _fallMs:    2400 + Math.random() * 1200
            readonly property real _w:         7  + Math.random() * 6
            readonly property real _h:         7  + Math.random() * 6
            readonly property color _color:    root._palette[index % root._palette.length]

            x: _startX
            y: -14
            width:  _w
            height: _h

            Rectangle {
                anchors.fill: parent
                color:        piece._color
                opacity:      0.88
                radius:       2
            }

            ParallelAnimation {
                running: root.visible
                loops: 1

                // Fall to just below the window bottom
                NumberAnimation {
                    target:   piece
                    property: "y"
                    from:     -14
                    to:       root.height + 20
                    duration: piece._fallMs
                    easing.type: Easing.InQuad
                }

                // Horizontal drift
                NumberAnimation {
                    target:   piece
                    property: "x"
                    from:     piece._startX
                    to:       piece._startX + piece._wobble
                    duration: piece._fallMs
                    easing.type: Easing.SineCurve
                }

                // Spin
                NumberAnimation {
                    target:   piece
                    property: "rotation"
                    from:     piece._startRot
                    to:       piece._startRot + 720
                    duration: piece._fallMs
                }

                // Fade out in the last third of the fall
                SequentialAnimation {
                    PauseAnimation { duration: piece._fallMs * 0.67 }
                    NumberAnimation {
                        target:   piece
                        property: "opacity"
                        from:     1.0
                        to:       0.0
                        duration: piece._fallMs * 0.33
                    }
                }
            }
        }
    }

    // ── Auto-hide ─────────────────────────────────────────────────────────────
    Timer {
        id: autoHide
        interval: 3500
        onTriggered: {
            confettiRepeater.model = 0
            root.visible = false
        }
    }
}
