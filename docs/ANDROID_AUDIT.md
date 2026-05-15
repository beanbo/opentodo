# Android UI / Platform Audit

**Date:** 2026-05-15
**Build target:** `cmake -S . -B build-android` with Qt 6.11 Android kit, `minSdk 26`, `targetSdk 34`.
**Scope:** `qml/layouts/MobileLayout.qml`, `qml/Main.qml` (Android paths), `android/AndroidManifest.xml`, `android/src/org/opentodo/*.java`, `src/services/notificationservice_android.cpp`, `src/services/hotkeyservice_stub.cpp`, `src/storage/filemanager.cpp` (Android paths), `src/main.cpp` (Android paths).
**Build available?** No — this audit is static analysis only.

## Verdict at a Glance

The Android target compiles and probably launches, but the Mobile UI is **largely non-functional as a real Todoist replacement on a phone**. The single biggest issue: `MobileLayout.qml` never loads any of the non-task-list views (Settings, Productivity, Notifications, AI, Board, Calendar, Trash). Tapping those entries in the drawer only changes the top-bar title; the body keeps showing the inbox/today task list. Combined with the broken notification-permission JNI bridge (no `onRequestPermissionsResult` override) and the absence of any Android back-button handler, **this build would frustrate a user within the first minute**.

There are no crash-level bugs found (so no production code modified per the audit rules), but several findings should be fixed before any beta APK ships.

---

## 1. `qml/layouts/MobileLayout.qml`

### 1.1 HIGH — Full-page views are completely unreachable on mobile

**File:** `qml/layouts/MobileLayout.qml` (entire file)
**Scenario:** User taps **Настройки**, **Продуктивность**, **Уведомления**, **✨ Помощник**, **Доска**, **Календарь**, or **Корзина** in the sidebar drawer.
**Result:** The drawer closes (well, it doesn't actually — see 1.6), the top-bar title updates to "Настройки" / "Продуктивность" / etc., but **the screen still shows the inbox task list** because `MobileLayout` has no `Loader` for any of `views/SettingsView.qml`, `views/ProductivityView.qml`, `views/NotificationsView.qml`, `views/AIView.qml`, `views/BoardView.qml`, `views/CalendarView.qml`, `views/TrashView.qml`. Compare with `DesktopLayout.qml:582-614` which has a full `Loader { id: fullPageLoader; source: { switch(filterMode) ... } }` block.

Confirmation from `MobileLayout.qml`:
- Line 73 introduces a placeholder `property var notificationsViewItem: null` "to be wired from any Loader that loads NotificationsView on mobile", but the Loader itself was never added.
- Line 240 unconditionally renders `ListView { id: taskListView; model: taskViewModel }` for **every** filter mode.

**Severity:** HIGH (≥ 50% of advertised features are dead on Android).
**Fix:** Replicate the `fullPageLoader` switch from `DesktopLayout.qml:582-614` immediately above the `taskListView` `Item { Layout.fillWidth: true; Layout.fillHeight: true }` block, with `active: isFullPageView` and `visible: active`. Define `isFullPageView` as `[Settings, Productivity, Notifications, AI, Board, Calendar, Trash].includes(taskViewModel.filterMode)`. Conditionally hide the QuickAdd bar and task list when `isFullPageView` is true. Mark `notificationsViewItem = fullPageLoader.item` inside `onLoaded` so the existing `Connections` block (line 75-82) starts working.

---

### 1.2 HIGH — No Android back-button handler anywhere

**File:** `qml/Main.qml`, `qml/layouts/MobileLayout.qml`
**Scenario:** Drawer is open, or task editor is open via `taskEditorStack.push`, or `searchOverlay` is open. User presses the system back button (gesture or button).
**Result:** The window's default `closing` handler accepts the close and **the app quits**. The drawer/popup/StackView page is dismissed by the OS killing the activity, not by app-level navigation.
**Confirmation:** `Grep` for `Window.closing`, `onClosing`, `Keys.onBackPressed`, `Qt.Key_Back` in the entire QML tree returns zero matches.

**Severity:** HIGH. This is the single biggest Android idiom violation.
**Fix:** In `qml/Main.qml`, add:

```qml
onClosing: function(close) {
    const layout = layoutLoader.item
    if (Qt.platform.os !== "android") return
    if (layout && layout.handleBack && layout.handleBack()) {
        close.accepted = false
    }
}
```

In `qml/layouts/MobileLayout.qml`, expose `function handleBack()` that returns `true` after popping `taskEditorStack` (if depth > 0), closing `searchOverlay`, or closing `sidebarDrawer` — `false` only when nothing was handled (truly exiting).

---

### 1.3 MEDIUM — Touch targets below Android 48 dp minimum

**File:** `qml/layouts/MobileLayout.qml`, `qml/components/SidebarContent.qml`
**Scenario:** A user taps a project / label / filter / settings entry in the sidebar drawer on a 360 × 640 device with a finger of average width.
**Result:** Targets are 36 px tall (`SidebarContent.qml:245`, `:415`, etc.) — well below the 48 dp Android Material guideline. Misses are common on the chevron toggle (line 754: `height: 16` with a 24 px negative-margin hit-area = 24 px effective, still below 48 dp).
**Confirmation:** `sidebarItemHeight: 36` declared at `MobileLayout.qml:18` but never actually applied to any `SidebarContent.qml` row (sidebar component has its own hard-coded `height: 36`).

**Severity:** MEDIUM. App works, but mis-taps are frequent.
**Fix:** Bump sidebar row heights from 36 → 48 when `Qt.platform.os === "android"` (or unconditionally). Same for the FAB hit-area (already 56, OK), the chevron toggles (16 → use a 48 dp `Item` wrapper with `MouseArea.anchors.margins: -16`), and the priority circle (Task row, around `TaskItem.qml:600+`).

---

### 1.4 MEDIUM — Search overlay sized in raw pixels; soft keyboard covers the input

**File:** `qml/layouts/MobileLayout.qml:537-545`
**Scenario:** User taps the search icon. The `Popup` opens with `width/height: Overlay.overlay.width/height`. User taps the search field. The soft keyboard slides up.
**Result:** Because no `android:windowSoftInputMode` is declared in `AndroidManifest.xml` (default is `adjustResize`), the Overlay's height is recomputed when Qt redraws — but Qt 6's Android backend has historically lagged on this. The `Popup` was sized at open time to full screen, and its inner `ListView` of search results goes behind the keyboard. The user can scroll, but the search **field** at the top of the popup may or may not stay visible depending on Qt version (Qt 6.11 has it mostly working, but the popup's `height` does not re-evaluate on keyboard).
**Severity:** MEDIUM. Search is usable but cramped.
**Fix:** Add `android:windowSoftInputMode="adjustResize"` to the `<activity>` element in `AndroidManifest.xml:36`. Inside the popup, anchor the search bar `Rectangle` to the top of the parent (already done) and rely on `Layout.fillHeight: true` for the results list — confirm `Overlay.overlay.height` shrinks when the IME opens. Optionally wrap the results list in a `ScrollView` with `contentHeight` clamped to `parent.height - Qt.inputMethod.keyboardRectangle.height` when `Qt.inputMethod.visible`.

---

### 1.5 MEDIUM — `TaskEditor.qml` has a fixed `width: 400`, ignores StackView fill

**File:** `qml/components/TaskEditor.qml:19-20`, used via Loader in `qml/layouts/MobileLayout.qml:519-530`
**Scenario:** User taps a task in the inbox on a 360 × 640 device. The mobile editor StackView pushes a page whose `Loader { Layout.fillWidth: true; Layout.fillHeight: true; source: "TaskEditor.qml" }` loads the editor.
**Result:** The Loader fills, but the loaded item ignores it because `TaskEditor.qml` has hard-coded `width: 400` and `x: _targetX + width` for slide-in animation. On a 360-px screen the editor overflows the right edge by 40 px; the slide-in starts at `x = 800` and ends at `x = 0`, which works visually but always renders a 400-px-wide editor inside a 360-px viewport. UI elements at the right edge are clipped.
**Severity:** MEDIUM. The editor is usable, but content on the right side (deadline pill, duration pill, save button row alignment) is partially obscured on small phones.
**Fix:** In `qml/components/TaskEditor.qml:19`, change `width: 400` → `width: parent ? parent.width : 400` so the editor adapts when parented to a Loader that has been size-set by Layout. Adjust the `_targetX` / slide-in animation to use the resolved width.

---

### 1.6 LOW — Sidebar drawer does not auto-close on navigation

**File:** `qml/layouts/MobileLayout.qml:32-67`
**Scenario:** User opens the drawer (hamburger), taps "Today". The view switches but the drawer stays open, covering most of the screen.
**Result:** User must tap outside the drawer to dismiss it after every navigation. Other mobile apps (Gmail, Todoist, etc.) close their nav drawer automatically on item-click.
**Severity:** LOW. UX papercut, no data risk.
**Fix:** In the `Drawer { id: sidebarDrawer }` block, listen to `sidebarViewModel.navigationRequested` and close the drawer:

```qml
Connections {
    target: sidebarViewModel
    function onNavigationRequested(view, itemId) {
        sidebarDrawer.close()
    }
}
```

---

### 1.7 LOW — Status-bar / notch safe-area not handled

**File:** `qml/Main.qml:114-115` (window sizing), `qml/layouts/MobileLayout.qml:90-100` (topBar at y=0)
**Scenario:** Phones with display cutouts (Pixel 6+, Galaxy S20+, etc.) running edge-to-edge content.
**Result:** Qt's QtActivity defaults to non-edge-to-edge, and `AppTheme` declares an opaque `statusBarColor #c43b30`, so the status bar reserves its own space and OpenTodo content begins below it. **This works.** However, no `SafeArea`-equivalent API is used, so when running on a device that draws under cutouts (Android 15+ default behavior on some OEMs), the hamburger button and title may sit under the cutout.
**Severity:** LOW (probably OK on most current devices, will become a problem on Android 15+).
**Fix:** Either keep the opaque status bar (current state — works) or, for an edge-to-edge experience, query `WindowInsets.getInsets(systemBars())` via JNI and expose it as a context property `statusBarHeight`; apply it as the `topBar`'s top padding.

---

### 1.8 LOW — Hamburger glyph (☰) instead of a proper menu icon

**File:** `qml/layouts/MobileLayout.qml:116-134`
**Scenario:** Visual polish.
**Result:** Three Unicode-glyph icons (`☰`, `⌕`, `+`) instead of crisp Material icons. The `B-6.34` comment notes `qrc:/icons/menu.svg doesn't exist`. On Android the Unicode glyphs render with the system font, which can look out of place against Material Design components.
**Severity:** LOW. Cosmetic.
**Fix:** Add proper SVG icons to `resources/icons/` and reference them via the existing `qrc` path, or use `Material.IconLabel` / `IconImage` from `QtQuick.Controls.Material.impl` (less portable).

---

### 1.9 LOW — Top bar has no Android-specific `topInset`

**File:** `qml/layouts/MobileLayout.qml:90-104`
**Scenario:** Same as 1.7, but specifically about the topBar's `Layout.preferredHeight: 56` value.
**Result:** 56 px is the standard Material AppBar height. With the status bar at ~24 dp on top (which Qt + opaque statusBarColor places **above** the topBar, not overlapping), the total usable header height is fine on a 640-px-tall device. On taller devices (915 px), the proportion is still acceptable.
**Severity:** LOW.
**Fix:** None needed; flagged here for completeness.

---

## 2. `android/src/org/opentodo/NotificationHelper.java`

### 2.1 HIGH — `onRequestPermissionsResult` is never bridged from QtActivity

**File:** `android/src/org/opentodo/NotificationHelper.java:169-189`, `android/AndroidManifest.xml:36` (QtActivity, not subclassed)
**Scenario:** App starts on Android 13+. `SyncIndicator.qml:14-24` invokes `notificationService.requestPermission()`, which calls `NotificationHelper.requestNotificationPermission(context)`, which calls `ActivityCompat.requestPermissions(...)`. User taps **Allow** or **Deny** in the system dialog.
**Result:** Android calls `QtActivity.onRequestPermissionsResult(...)` — but `QtActivity` is the unmodified Qt base class, with no override that forwards request code 42 to `NotificationHelper.onPermissionsResult(granted)`. The bridge never fires; the C++ JNI callback `Java_org_opentodo_NotificationHelper_onPermissionResult` never runs; `permissionGranted` / `permissionDenied` signals are never emitted. The flag `settingsViewModel.notificationPermissionAsked = true` is already set unconditionally by `SyncIndicator.qml:23` immediately after the call, so the prompt is never shown again.
**Net result:** On Android 13+, the user grants or denies once, but the app's internal state never reflects the answer and no notifications are scheduled until app reinstall.
**Severity:** HIGH.
**Fix:** Subclass `QtActivity`:

```java
// android/src/org/opentodo/OpenTodoActivity.java
package org.opentodo;
import org.qtproject.qt.android.bindings.QtActivity;
import android.content.pm.PackageManager;
public class OpenTodoActivity extends QtActivity {
    @Override
    public void onRequestPermissionsResult(int code, String[] perms, int[] results) {
        super.onRequestPermissionsResult(code, perms, results);
        if (code == 42) {
            boolean granted = results.length > 0
                && results[0] == PackageManager.PERMISSION_GRANTED;
            NotificationHelper.onPermissionsResult(granted);
        }
    }
}
```

And update `AndroidManifest.xml:36`:
```xml
android:name="org.opentodo.OpenTodoActivity"
```

Plus the `meta-data android.app.lib_name = "OpenTodo"` already correctly points to the C++ entry point.

---

### 2.2 MEDIUM — `notificationPermissionAsked` flipped before result arrives

**File:** `qml/components/SyncIndicator.qml:14-24`
**Scenario:** First launch on Android 13+. SyncIndicator runs `Component.onCompleted`, calls `requestPermission()`, then on the very next line sets `settingsViewModel.notificationPermissionAsked = true`.
**Result:** Even if 2.1 is fixed, the persistent flag is set synchronously, so on **denial** the app records "user has been asked" and never re-prompts even after the user notices reminders don't fire and looks for a way to enable them.
**Severity:** MEDIUM.
**Fix:** Move the flag-set into the `permissionGranted` / `permissionDenied` signal handlers in `main.cpp:450-457` (already done there!) — and **remove** the unconditional set from `SyncIndicator.qml:23`.

---

### 2.3 LOW — `cancelAllNotifications` cannot cancel pending AlarmManager intents

**File:** `android/src/org/opentodo/NotificationHelper.java:150-157`
**Scenario:** User taps "Cancel all" in some hypothetical bulk-clear UI (not currently exposed).
**Result:** Only already-shown notifications are cleared; pending alarms still fire later. The Java code acknowledges this in a comment.
**Severity:** LOW (no caller exercises this path today).
**Fix:** In C++, maintain a set of active notif IDs in `QSettings` and iterate when bulk-cancelling. Or document the limitation explicitly in `NotificationService::cancelAll()`.

---

## 3. `android/AndroidManifest.xml`

### 3.1 MEDIUM — `SCHEDULE_EXACT_ALARM` requested but `canScheduleExactAlarms()` never checked

**File:** `android/AndroidManifest.xml:25`, `src/services/notificationservice_android.cpp:114-157` (scheduleNotification)
**Scenario:** App schedules a reminder on Android 14+ (API 34). The manifest declares `SCHEDULE_EXACT_ALARM` but on Android 14, this is **no longer auto-granted** for non-calendar/alarm-clock apps. The user must visit Settings → Apps → OpenTodo → Alarms & reminders → toggle on.
**Result:** Calling `AlarmManager.setExactAndAllowWhileIdle()` throws `SecurityException`. The current Java code (`NotificationHelper.java:118-122`) does NOT wrap this in a try/catch, so the exception propagates and may crash the JNI thread.
**Severity:** MEDIUM (depends on Android version; rising risk over time).
**Fix:**
1. Add `<uses-permission android:name="android.permission.USE_EXACT_ALARM" />` — this is the right permission for a reminder app, auto-granted on install on API 33+, no user toggle.
2. In `NotificationHelper.scheduleNotification`, fall back to `setAndAllowWhileIdle()` (inexact) when `am.canScheduleExactAlarms()` returns false on API 31+. Wrap the exact-alarm call in try/catch as defense in depth.

```java
if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S && !am.canScheduleExactAlarms()) {
    am.setAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, triggerAtMillis, pi);
    return;
}
try {
    am.setExactAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, triggerAtMillis, pi);
} catch (SecurityException e) {
    am.setAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, triggerAtMillis, pi);
}
```

---

### 3.2 MEDIUM — No `windowSoftInputMode`; popup sizing fights the keyboard

**File:** `android/AndroidManifest.xml:35-46`
**Scenario:** Same as 1.4.
**Severity:** MEDIUM.
**Fix:** Add `android:windowSoftInputMode="adjustResize"` to the `<activity>` opening tag.

---

### 3.3 LOW — `FOREGROUND_SERVICE` permission requested but never used

**File:** `android/AndroidManifest.xml:9`
**Scenario:** Play Store review.
**Result:** Permission is requested but no `<service>` of type `dataSync` is registered (a comment at lines 10-19 explicitly says the service was removed). Play Store flags permissions without matching usage; this is a benign warning but worth cleaning up.
**Severity:** LOW.
**Fix:** Remove line 9 `<uses-permission android:name="android.permission.FOREGROUND_SERVICE" />` until a foreground service is actually re-added. If `RECEIVE_BOOT_COMPLETED` is similarly unused (no `BootReceiver` exists per the manifest comment), remove it as well.

---

### 3.4 LOW — `screenOrientation="unspecified"` allows landscape; mobile layout assumes portrait

**File:** `android/AndroidManifest.xml:40`
**Scenario:** Tablet or phone rotated to landscape.
**Result:** `MobileLayout` keeps the same top-bar + drawer + task list arrangement, which works at landscape but wastes vertical space. No critical issue, but on a tablet the desktop layout (DesktopLayout) would be a far better fit. The choice between mobile and desktop layouts is gated by `Qt.platform.os === "android"` at `qml/Main.qml:166`, never by width — so a 10-inch Android tablet gets the same cramped mobile UI as a 5-inch phone.
**Severity:** LOW (tablets are an edge case for this project).
**Fix:** Either set `screenOrientation="portrait"` for phones only, or make the layout switch in `Main.qml:166` size-based: `Qt.platform.os === "android" && Screen.width < 700 ? mobile : desktop`.

---

## 4. `src/services/notificationservice_android.cpp` (JNI bridge)

### 4.1 LOW — Race window in `s_pendingService` global

**File:** `src/services/notificationservice_android.cpp:18-40`
**Scenario:** User taps the system permission dialog at the same time another call to `requestPermission()` is in flight.
**Result:** The static pointer holds only one pending service; a second request before the first's result arrives overwrites it. The first request's signal silently disappears.
**Severity:** LOW (only one `AndroidNotificationService` is ever constructed, and `requestPermission()` is called at most once per session).
**Fix:** Document the single-flight assumption in a comment, or guard with `if (s_pendingService) return; // already pending`.

---

### 4.2 LOW — `allocateId` reads/writes QSettings on every notification

**File:** `src/services/notificationservice_android.cpp:42-56`
**Scenario:** A user with 500 reminders triggers a reload.
**Result:** Each call walks the entire `QVariantMap` from QSettings, mutates it, writes it back. Acceptable today, but O(n) per notification gets noticeable past a few hundred entries.
**Severity:** LOW.
**Fix:** Cache `notif/idmap` in an in-memory `QHash` on first access; flush to QSettings on `aboutToQuit`.

---

### 4.3 LOW — Notification channel created with default importance only

**File:** `src/services/notificationservice_android.cpp:66-86`
**Scenario:** User receives a reminder; expects it to make sound + buzz vibrator (priority HIGH).
**Result:** Channel is created with `importance = 3` (`IMPORTANCE_DEFAULT`), not `IMPORTANCE_HIGH = 4`. The Java side then builds the notification with `PRIORITY_HIGH` (`NotificationHelper.java:90`), but channel importance trumps notification priority on API 26+. So reminders show without heads-up display.
**Severity:** LOW (reminders still appear, just less prominently).
**Fix:** Pass `4` (`IMPORTANCE_HIGH`) instead of `3` for the reminders channel. Already done in `NotificationReceiver.java:29` for the recreate path.

---

## 5. Hotkey stub on Android

### 5.1 No findings

**File:** `src/services/hotkeyservice_stub.cpp`
**Status:** Verified — pure no-op, no Win32 includes leak through. `main.cpp:174-201` is correctly guarded with `#ifndef Q_OS_ANDROID` around `registerQuickAdd()` and the related signal wiring, so the stub is never asked to do anything that would matter.

---

## 6. Storage / lifecycle

### 6.1 MEDIUM — No `applicationStateChanged` flush

**File:** `src/main.cpp:149-152` (`aboutToQuit` flush) — but no `applicationStateChanged` handler
**Scenario:** Android user edits a task, sees the QuickAdd close, presses Home before the 1-second autosave debounce fires (`FileManager.cpp:27`). System sits the app in background; OS kills the process under memory pressure 30 minutes later.
**Result:** `aboutToQuit` is NOT emitted on Android when the OS force-stops the app. The 1-second-debounce write never landed. The edit is lost.
**Severity:** MEDIUM. Recovery is possible because previous `state.json` is intact, but unsaved edits made in the last second before backgrounding are lost.
**Fix:** In `src/main.cpp` somewhere near line 153, add:

```cpp
QObject::connect(&app, &QGuiApplication::applicationStateChanged, fileManager,
    [fileManager](Qt::ApplicationState state) {
        if (state == Qt::ApplicationSuspended || state == Qt::ApplicationInactive)
            fileManager->saveImmediate();
    });
```

Also flush the SyncEngine pending commit log in the same handler.

---

### 6.2 LOW — `state.json` lives in scoped app-private storage (correct, but undocumented)

**File:** `src/storage/filemanager.cpp:19-24`
**Scenario:** Audit confirmation only.
**Result:** Android branch uses `QStandardPaths::AppDataLocation` directly, which on Android maps to `getExternalFilesDir(null)` (or `getFilesDir()` if no external storage). This is scoped storage — survives app restarts, deleted on uninstall, requires no permission. Good.
**Severity:** LOW (informational).
**Fix:** Add a comment in `FileManager.cpp:20` clarifying that on Android this is `Context.getFilesDir()/...`, not external SD.

---

### 6.3 LOW — `TaskExporter` exports to `DocumentsLocation` — broken under scoped storage

**File:** `src/services/taskexporter.cpp:33-36`, `:71-74`, `:120-123`
**Scenario:** User taps "Export CSV" on Android 11+.
**Result:** `QStandardPaths::DocumentsLocation` on Android returns a path inside the app's private external storage (`/storage/emulated/0/Android/data/org.opentodo.app/files/Documents/`), not the user-visible `Documents/` folder. The file IS written, but the user can't find it via a file manager because that path is hidden from MediaStore on Android 11+.
**Severity:** LOW (export still works, file is just hard to retrieve).
**Fix:** On Android, use the Storage Access Framework (`Intent.ACTION_CREATE_DOCUMENT`) via a Java helper to let the user pick a destination. Larger task — flag for backlog, not blocking.

---

## Speculative / Low-Confidence Findings

These were noticed in passing but I'd want to verify on a real device before acting on them.

- **`Material.theme: Dark`** in `qml/Main.qml:120` is applied per-window. On Android the Material style typically renders inside `ApplicationWindow`; the inner `Drawer`'s background is hard-coded to `contentColor` (`MobileLayout.qml:43`). I'd expect this to be fine, but Material's elevation shadows on Drawer can leak through with a transparent background — couldn't verify without rendering.
- **`Drawer { width: 280 }`** at 360 px = 77% of screen. Probably fine. On a very narrow phone (320 px or QVGA) it would overlap the entire viewport; the drawer's modal backdrop has zero clickable area to dismiss. Could add `Math.min(280, parent.width - 56)`.
- **`FAB at bottomMargin: 16`** (`MobileLayout.qml:344`) — on devices with a gesture navigation bar, 16 px clearance may collide with the swipe-up zone. Should probably be `Math.max(16, gestureNavInset)`. Not testable without device.
- **`QApplication`** is used (not `QGuiApplication`) at `src/main.cpp:76`. On Android this drags in QtWidgets even though no widget is ever shown. Not a bug, but increases APK size by ~3 MB. Switching to `QGuiApplication` would also require removing any widget includes — likely a separate refactor.
- **`SearchOverlay` row height of 56 px** (`MobileLayout.qml:622`) is good — exactly the Material 56 dp list-item minimum. Confirmed OK.
- The `viewTitleLabel` in `MobileLayout.qml:140-170` does not include `Trash` (Корзина). Tapping "Корзина" in the sidebar would show "Задачи" as the fallback title. Minor cosmetic, but goes with finding 1.1 — fix it together.

---

## Summary by Severity

| Severity | Count | Findings |
|----------|-------|----------|
| HIGH | 3 | 1.1, 1.2, 2.1 |
| MEDIUM | 6 | 1.3, 1.4, 1.5, 2.2, 3.1, 3.2, 6.1 |
| LOW | 8 | 1.6, 1.7, 1.8, 1.9, 2.3, 3.3, 3.4, 4.1, 4.2, 4.3, 5.1, 6.2, 6.3 |

The three HIGH findings (full-page views unreachable, no back-button handler, broken POST_NOTIFICATIONS bridge) compound: a user opening the app on Android 13+ will tap Settings → see no change → tap Notifications → see no change → press Back → app exits abruptly. **All three should be fixed before any beta APK ships.**

The MEDIUM findings affect daily UX (mistapped sidebar items, hidden search input under keyboard, lost edits on background kill) but do not block first-time-use. They should be fixed before a stable release.

No crash-level bugs found that warrant immediate code edits per the audit rules.
