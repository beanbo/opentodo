# Privacy Policy

_Last updated: 2026-05-17_

> This is a template provided by the OpenTodo project. Fields tagged `<TODO>`
> must be filled in (or removed) by whoever distributes a particular build of
> OpenTodo before publication. If you are monetising OpenTodo, redistributing
> it under a different name, or operating a hosted version, you should review
> this document with a lawyer in your jurisdiction.

OpenTodo is open-source software released under the [MIT License](../LICENSE).
This document describes what the OpenTodo application does, and does not do,
with your data.

The English text below is the authoritative version. Russian-language users
should note: the application UI is fully translated (`ru`, `de`, `uk`, `be`),
but this policy is currently only maintained in English. Translations are
welcome via pull request — see [CONTRIBUTING.md](../CONTRIBUTING.md).

---

## 1. Overview

OpenTodo stores all of your data locally on the device where the app runs.
There is no OpenTodo account, no OpenTodo backend server, and no analytics
provider built into the application. Cloud sync, AI assistance, and usage
telemetry are each **opt-in**, disabled by default, and clearly marked in the
Settings screen. When all of them are off, OpenTodo makes no outbound network
requests other than the occasional check for a new release (if you enable
update checks).

## 2. What we collect

The following describes what data may leave your device, and under which
conditions.

### 2.1 Locally on device (always)

Stored in your operating system's per-user application-data folder
(`$APPDATA/OpenTodo/` on Windows and Linux, the Android per-app private
storage on Android, IndexedDB-backed storage on Web/WASM):

- Tasks, projects, sections, labels, comments, reminders, filters.
- Application settings and UI preferences (theme, sort order, sidebar layout,
  hotkeys, locale).
- Sync metadata (Hybrid Logical Clock state, per-field CRDT timestamps,
  pending local commits, device ID).
- The pending commit log under `local_log/` and rotated state snapshots.

This data never leaves the device unless you explicitly enable one of the
opt-in features below.

### 2.2 If Google Drive sync is enabled

When you sign in with Google in `Settings → Sync`:

- OpenTodo requests the OAuth scopes
  `https://www.googleapis.com/auth/drive.file` (read/write access **only to
  files that OpenTodo itself creates or opens** in your Drive), plus the
  standard `email` and `profile` scopes so the connected account address can
  be displayed in the app.
- Your task state (`state.json`), commit log (`log/<uuid>.json`), and lock
  file (`.lock`) are uploaded to a folder in **your own** Google Drive.
- OpenTodo does not have, and cannot request, access to any other files in
  your Drive.
- The OAuth access and refresh tokens are stored locally in your
  per-user `QSettings` (registry on Windows, `~/.config/` on Linux, etc.).
- The Google account email address returned by the userinfo endpoint is
  displayed in the Sync settings screen and stored locally with your settings
  so you can see which account you are connected to.

### 2.3 If Claude AI integration is enabled

When you paste an Anthropic API key into `Settings → AI`:

- Task content (title, description, and where relevant the surrounding
  project / subtask context) is sent over HTTPS to
  `https://api.anthropic.com/v1/messages` so Claude can suggest subtasks,
  reword tasks, or summarise.
- Requests are sent directly from your device to Anthropic using **your own**
  API key. OpenTodo has no proxy, no server, and no shared key.
- The API key itself is stored locally with XOR obfuscation derived from
  `QSysInfo::machineUniqueId()`. This is obfuscation, not real encryption —
  if you need stronger protection, install QtKeychain and OpenTodo will use
  it when present.
- Your key, prompts, and responses do not pass through any OpenTodo-operated
  infrastructure.

### 2.4 If anonymous telemetry is enabled

Telemetry is **off by default**. The first run shows a prompt; nothing is
sent unless you explicitly opt in. When enabled
(`Settings → Privacy → Telemetry`):

- A random per-installation UUID is generated locally and sent as
  `anonymous_id`. It is **not** derived from any hardware identifier and you
  can reset it at any time from the Settings screen (this issues a fresh UUID
  on the next event).
- Only events from a fixed allowlist of property keys are accepted; anything
  not on the list is dropped at the source. As of this writing the
  allowlist is: `event_type`, `duration_ms`, `task_count`, `project_count`,
  `platform`, `view`, `priority`, `label_count`, `filter_used`,
  `sync_success`, `error_code`.
- Event names, the anonymous ID, the app version, and a UTC timestamp are
  batched and POSTed every 5 minutes (or on app quit) to the configured
  endpoint. The default endpoint baked into open-source builds is
  `https://telemetry.opentodo.app/track`. Forks and self-built copies may
  point at a different endpoint or none at all.
- No task content, project names, label names, search terms, file paths,
  email addresses, names, or IP-address-derived geolocation is sent. The
  collected fields are designed to answer questions like "is the calendar
  view used?" or "do completed-task counts skew low on Android?" — not "who
  is this user?".

## 3. What we DON'T collect

OpenTodo intentionally does not collect or transmit:

- Your name, email, phone number, or any account identifier — the app has no
  account system and no sign-up flow.
- Your IP address as a user identifier. (Any HTTP request you initiate
  obviously exposes an IP to the destination server — Google, Anthropic, or
  the optional telemetry endpoint — but OpenTodo does not log it.)
- Your physical location. The optional `LocationService` (only present in
  builds with `Qt Positioning`, used for location-based reminders) reads
  positioning data **locally only** to fire geofence events; coordinates are
  never sent off the device.
- Browsing or usage analytics through any third-party SDK (Google Analytics,
  Firebase, Sentry, Amplitude, Mixpanel, Segment, PostHog, etc.). None of
  these are linked into the binary.
- Advertising identifiers, tracking pixels, or ad networks.
- Crash reports — the bundled `crashlogger` writes locally and does not
  upload.
- Contents of files outside the OpenTodo app folder.

## 4. Third parties

The two third parties OpenTodo can talk to, and only at your request, are:

- **Google LLC** — if you enable Drive sync. Subject to the
  [Google Privacy Policy](https://policies.google.com/privacy) and
  [Google API Services User Data Policy](https://developers.google.com/terms/api-services-user-data-policy).
- **Anthropic, PBC** — if you enable the Claude integration. Subject to the
  [Anthropic Privacy Policy](https://www.anthropic.com/legal/privacy) and
  [Anthropic Usage Policy](https://www.anthropic.com/legal/aup).

OpenTodo has no other third-party network dependencies at runtime.

## 5. Data retention

- **Local data** is retained until you delete it from within the app, clear
  the app's storage from your OS, or uninstall the application. Deleted
  tasks, projects, etc. are soft-deleted (marked with `is_deleted=true`) so
  the change can converge across sync devices; soft-deleted in-memory
  tombstones older than 30 days are pruned on load (see
  `DataStore::pruneTombstones` in `src/storage/datastore.cpp`). On-disk
  history of compacted commits may persist longer if you have not run a
  snapshot compaction.
- **Google Drive data** is retained in your Drive until you remove it. You
  can delete OpenTodo's Drive folder at any time from drive.google.com.
  Revoking the OAuth grant at
  [myaccount.google.com/permissions](https://myaccount.google.com/permissions)
  stops further uploads but does not delete files already there.
- **Anthropic requests** are subject to Anthropic's retention policy; refer
  to their documentation. OpenTodo does not retain a copy of prompts or
  responses beyond what is displayed in the AI dialog.
- **Telemetry events**, if enabled, are retained by whoever operates the
  configured endpoint. For the default `telemetry.opentodo.app` endpoint
  the retention is `<TODO — endpoint operator must document this>`.

## 6. Your rights

Because all data is local and the app holds no account, you can exercise
data-subject rights yourself, immediately, without going through anyone:

- **Access / portability**: export your data as JSON (`Settings → Backup`),
  iCalendar (`.ics` via `IcsExporter`), or Markdown (`TaskExporter`).
- **Erasure**: delete tasks/projects from the UI, or remove the entire
  `$APPDATA/OpenTodo/` folder. If sync is enabled, also delete the
  OpenTodo folder from Google Drive and revoke the OAuth grant.
- **Rectification**: edit any task, project, or setting in-place.
- **Consent withdrawal**: each opt-in toggle (Drive sync, AI integration,
  telemetry) can be turned off at any time in Settings, and you can reset
  the anonymous telemetry ID on demand.

If you nonetheless need to contact the maintainer about your data, see
section 9.

## 7. Children's privacy

OpenTodo is a general-purpose productivity application and is not directed
at children under 13 (or under 16 where applicable under GDPR). The app
does not knowingly request or process information from children. Parents or
guardians supervising a child's use of OpenTodo should disable the optional
Drive sync and AI integration features.

## 8. Security

- Atomic writes via `QSaveFile` protect against partial writes from crashes.
- OAuth tokens are stored using Qt's `QSettings` backend (registry on
  Windows, plist on macOS, `~/.config/` on Linux). They are not encrypted at
  rest by OpenTodo itself; the OS-level account protection is the boundary.
- The Anthropic API key is XOR-obfuscated with a machine-derived secret —
  this defeats casual inspection but is not real encryption. Install
  [QtKeychain](https://github.com/frankosterfeld/qtkeychain) for stronger
  protection if your distribution does not already include it.
- All outbound HTTP traffic is HTTPS.

OpenTodo is open source — you can read every line of code that touches your
data in this repository.

## 9. Contact

For privacy questions about a specific distribution of OpenTodo:

- Open an issue at `<TODO — repository URL, e.g. https://github.com/<owner>/opentodo/issues>`.
- Or email the maintainer: `<TODO — maintainer email>`.

## 10. Changes to this policy

Material changes will be reflected by updating the `Last updated` date at the
top of this document and noting the change in the log below.

| Date       | Change                                        |
|------------|-----------------------------------------------|
| 2026-05-17 | Initial template.                             |
| `<TODO>`   | `<TODO — describe future revisions here>`     |
