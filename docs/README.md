# OpenTodo — Documentation Index

This directory holds the long-form documentation that doesn't belong in the
top-level [README.md](../README.md) or in [CLAUDE.md](../CLAUDE.md) (the
developer-facing canonical reference).

## Project documentation

| Document | Purpose |
|---|---|
| [ARCHITECTURE.md](ARCHITECTURE.md) | Layered architecture overview, data flow, sync model, CRDT design. |
| [WASM.md](WASM.md) | Building and running the WebAssembly (browser) target. |
| [HOTKEYS.md](HOTKEYS.md) | Keyboard shortcut reference for desktop. |
| [ANDROID_AUDIT.md](ANDROID_AUDIT.md) | Static audit of the Android target's UI / platform integration. |

## Legal / policy

| Document | Purpose |
|---|---|
| [PRIVACY.md](PRIVACY.md) | What data OpenTodo touches, what leaves the device, opt-in controls. |
| [TERMS.md](TERMS.md) | Terms of use covering the open-source distribution. |

Both `PRIVACY.md` and `TERMS.md` are **templates**. Fields tagged `<TODO>`
must be filled in (or removed) by whoever distributes a specific build —
maintainer email, repository URL, governing-law jurisdiction. If you are
shipping a commercial or hosted derivative, review them with a lawyer in
your jurisdiction.

## Translations

The application UI is translated into English, Russian, German, Ukrainian,
and Belarusian (see [`i18n/`](../i18n/)). The documents in this directory
are maintained in English only; translations are welcome via pull request
(see [CONTRIBUTING.md](../CONTRIBUTING.md)).
