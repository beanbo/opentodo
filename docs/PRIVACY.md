# Privacy Policy

_Last updated: 2026-11-03_

> This policy covers OpenTodo as published at
> <https://github.com/beanbo/opentodo>. If you redistribute a modified build
> that changes how data is handled (for example, one with a telemetry
> endpoint configured), ship your own revised version of this document.

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
provider built into the application. Cloud sync, AI assistance and imports
are each **opt-in** and disabled until you configure them in Settings. With
all of them off, the only outbound request OpenTodo makes is a daily check
for a new release (section 2.6).

## 2. What leaves your device, and when

### 2.1 Locally on device (always)

Stored in your operating system's per-user application-data folder
(`$APPDATA/OpenTodo/` on Windows and Linux, the Android per-app private
storage on Android, IndexedDB-backed storage on Web/WASM):

- Tasks, projects, sections, labels, comments, reminders, filters, templates.
- Application settings and UI preferences (theme, sort order, sidebar layout,
  hotkeys, locale).
- Sync metadata (Hybrid Logical Clock state, per-field CRDT timestamps,
  pending local commits, device ID).
- The pending commit log under `local_log/` and rotated state snapshots.
- A local crash log, which is never uploaded.

Secrets (OAuth tokens, AI API keys, the self-hosted sync token) are kept in
the OS keyring — Windows Credential Manager, macOS Keychain, or
libsecret/KWallet on Linux. Android and Web/WASM have no such keyring there,
so secrets live in the app's private settings storage (section 8).

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
- If you use **Share** on a project or task, OpenTodo uploads a snapshot of
  it to your Drive and creates a link that anyone who has it can open. Delete
  the file in Drive to revoke the link.

### 2.3 If self-hosted sync ("Свой ПК") is enabled

- On the PC, OpenTodo runs a small sync server that listens on `127.0.0.1`
  only. Other devices reach it through a tunnel you set up yourself (for
  example Tailscale Funnel), or over your local network if you turn on the
  Wi-Fi option, which binds the server to all interfaces.
- Every request must carry the bearer token shown in Settings. The data is
  your task state and commit log, exactly as with Google Drive, but it stays
  on your own PC.
- Plain `http://` is accepted only for private-network addresses; public
  hosts must use HTTPS.

### 2.4 If AI assistance is enabled

When you add an API key in `Settings → AI` (Anthropic Claude, OpenAI or an
OpenAI-compatible endpoint you specify, or Google Gemini):

- Task content (title, description, and where relevant the surrounding
  project / subtask context) is sent over HTTPS to the provider you chose so
  it can suggest subtasks, reword tasks, or summarise.
- Requests go from your device to that provider using **your own** API key,
  optionally through a proxy you configure. OpenTodo has no server and no
  shared key.
- Actions the assistant proposes (adding, completing or deleting tasks) are
  only applied after you confirm them.

### 2.5 If you import from Todoist

Importing with a Todoist API token sends that token to `api.todoist.com` to
download your tasks. The token is used for that import only and is not
stored. Importing a backup ZIP happens entirely offline.

### 2.6 Update checks

About once a day OpenTodo asks the GitHub API
(`api.github.com/repos/beanbo/opentodo/releases/latest`) whether a newer
release exists. The request carries no data about you or your tasks beyond
what any HTTP request exposes (your IP address and a User-Agent).

### 2.7 Telemetry

Builds published from this repository have **no telemetry endpoint**, so no
usage data is ever sent and the telemetry toggle does not appear in Settings.
The code can report a small allowlist of anonymous events if a distributor
configures their own collector at build time (`OPENTODO_TELEMETRY_ENDPOINT`);
such a build must describe that collection in its own privacy policy.

## 3. What we DON'T collect

OpenTodo intentionally does not collect or transmit:

- Your name, email, phone number, or any account identifier — the app has no
  account system and no sign-up flow.
- Your IP address as a user identifier. (Any HTTP request you initiate
  obviously exposes an IP to the destination server, but OpenTodo does not
  log it.)
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

OpenTodo talks to these services, and only for the features named:

- **GitHub** — the daily update check (section 2.6).
- **Google LLC** — Drive sync and sharing, and Gemini if you choose it as the
  AI provider. Subject to the
  [Google Privacy Policy](https://policies.google.com/privacy) and
  [Google API Services User Data Policy](https://developers.google.com/terms/api-services-user-data-policy).
- **Anthropic, PBC** — if you choose Claude. Subject to the
  [Anthropic Privacy Policy](https://www.anthropic.com/legal/privacy) and
  [Usage Policy](https://www.anthropic.com/legal/aup).
- **OpenAI** (or the OpenAI-compatible endpoint you enter) — if you choose it.
  Subject to the [OpenAI Privacy Policy](https://openai.com/policies/privacy-policy).
- **Todoist (Doist Inc.)** — only during an API-token import.

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
- **AI requests** are subject to the chosen provider's retention policy.
  OpenTodo does not retain a copy of prompts or responses beyond what is
  displayed in the AI dialog.
- **Telemetry**: none is collected by builds from this repository.

## 6. Your rights

Because all data is local and the app holds no account, you can exercise
data-subject rights yourself, immediately, without going through anyone:

- **Access / portability**: export your data as JSON (`Settings → Backup`),
  iCalendar (`.ics` via `IcsExporter`), or Markdown (`TaskExporter`).
- **Erasure**: delete tasks/projects from the UI, or remove the entire
  `$APPDATA/OpenTodo/` folder. If sync is enabled, also delete the
  OpenTodo folder from Google Drive and revoke the OAuth grant.
- **Rectification**: edit any task, project, or setting in-place.
- **Consent withdrawal**: each opt-in feature (sync, AI, imports) can be
  turned off at any time in Settings.

If you nonetheless need to contact the maintainer about your data, see
section 9.

## 7. Children's privacy

OpenTodo is a general-purpose productivity application and is not directed
at children under 13 (or under 16 where applicable under GDPR). The app
does not knowingly request or process information from children. Parents or
guardians supervising a child's use of OpenTodo should disable the optional
sync and AI features.

## 8. Security

- Atomic writes via `QSaveFile` protect against partial writes from crashes.
- On desktop, OAuth tokens, AI API keys and the self-hosted sync token are
  stored in the OS keyring through
  [QtKeychain](https://github.com/frankosterfeld/qtkeychain). On Android and
  Web/WASM they are stored in the app's private settings storage, protected
  by the platform's per-app sandbox rather than by encryption.
- Android backups of app data are disabled (`allowBackup=false`).
- Bearer tokens are never sent over plain HTTP to public hosts.

OpenTodo is open source — you can read every line of code that touches your
data in this repository.

## 9. Contact

- Open an issue at <https://github.com/beanbo/opentodo/issues>.
- To report a security problem privately, use
  [GitHub private vulnerability reporting](https://github.com/beanbo/opentodo/security/advisories/new).

## 10. Changes to this policy

Material changes will be reflected by updating the `Last updated` date at the
top of this document and noting the change in the log below.

| Date       | Change                                                        |
|------------|---------------------------------------------------------------|
| 2026-05-17 | Initial template.                                             |
| 2026-11-03 | First published version: keyring storage, self-hosted sync, additional AI providers, Todoist import, update checks; telemetry has no default endpoint. |
