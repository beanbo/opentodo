# Terms of Service

_Last updated: 2026-11-03_

> These Terms cover OpenTodo as published at
> <https://github.com/beanbo/opentodo>. If you are monetising OpenTodo,
> redistributing it under a different name, or operating a hosted version,
> ship your own terms and review them with a lawyer in your jurisdiction.

These Terms cover the OpenTodo desktop / mobile / WASM application
distributed from this repository. They are intentionally short because
OpenTodo is a local-first open-source app with no account, no subscription,
and no server operated by the OpenTodo project itself.

The English text below is the authoritative version; translations
(`ru`, `de`, `uk`, `be`, etc.) are welcome via pull request.

---

## 1. Acceptance

By installing, building, or using OpenTodo you accept these Terms. If you do
not accept them, do not use the application. Continued use after a change to
these Terms (signalled by a new `Last updated` date) constitutes acceptance
of the revised version.

## 2. License

OpenTodo is licensed to you under the [MIT License](../LICENSE). The MIT
license governs your right to use, copy, modify, and redistribute the
software. These Terms supplement — but do not override — the MIT license.

Third-party components bundled with or linked by OpenTodo (Qt 6 under LGPLv3,
OpenSSL, zlib, etc.) are listed in [NOTICE](../NOTICE) and remain governed by
their own licenses.

## 3. Permitted use

OpenTodo is intended for personal and small-team task management. You are
responsible for:

- The contents of any tasks, projects, comments, or attachments you create.
- Maintaining your own backups (see section 7).
- Compliance with the laws of your jurisdiction regarding the data you put
  into the application.
- Securing the device and operating-system account on which OpenTodo runs.

You agree not to use OpenTodo:

- To process personal data of others in violation of applicable data
  protection law.
- To store or transmit content that is illegal in your jurisdiction.
- In a way that violates the third-party terms referenced in section 5
  (Google Drive, AI providers, Todoist) if you enable those integrations.

## 4. No warranty

OpenTodo is provided **"AS IS"**, without warranty of any kind, express or
implied, including but not limited to the warranties of merchantability,
fitness for a particular purpose, and noninfringement, as set out in the MIT
license.

The maintainers and contributors do not guarantee that the software is
defect-free, that sync will always converge correctly under adverse network
conditions, that AI suggestions are accurate, or that the application will
remain available in any particular form.

## 5. Third-party services

Optional features integrate with external services. When you enable them,
you also agree to be bound by the relevant third party's terms:

- **Google Drive sync** — Google [Terms of Service](https://policies.google.com/terms)
  and [Privacy Policy](https://policies.google.com/privacy). OpenTodo
  requests only the narrow `drive.file` scope plus standard `email` /
  `profile`.
- **AI assistance** — the terms of the provider you choose:
  Anthropic [Commercial Terms of Service](https://www.anthropic.com/legal/commercial-terms)
  (or [Consumer Terms](https://www.anthropic.com/legal/consumer-terms),
  depending on your account) and [Usage Policy](https://www.anthropic.com/legal/aup);
  OpenAI [Terms of Use](https://openai.com/policies/terms-of-use); or the
  [Gemini API Additional Terms](https://ai.google.dev/gemini-api/terms).
  You are the API customer of that provider; OpenTodo is not a reseller or
  proxy.
- **Todoist import** — Doist [Terms of Service](https://doist.com/terms-of-service).

The OpenTodo project is not affiliated with, endorsed by, or sponsored by
Google, Anthropic, OpenAI, or Doist.

## 6. Accounts, subscriptions, fees

- **OpenTodo itself**: no account, no subscription, no fee. There is no
  paid tier and no in-app purchase.
- **Google Drive**: free up to Google's standard quota; subject to their
  own pricing if you exceed it.
- **AI API usage**: billed by your AI provider against your own API key.
  OpenTodo does not see your billing relationship and does not charge a
  margin. You alone are responsible for any cost incurred from sending
  requests through the AI integration. Set a usage limit with your provider
  if you want a hard ceiling.

## 7. Data loss

OpenTodo offers no guarantees about data durability beyond best effort. In
practice the app uses atomic writes, debounced autosave, a per-field CRDT
sync model, and an optional rotated-backup feature (`Settings → Backup`),
but you remain responsible for your own backups.

The maintainers and contributors are not liable for:

- Lost or corrupted tasks, projects, or settings.
- Sync conflicts that resolve in a way you did not expect.
- Data that becomes inaccessible because of an OS upgrade, a hardware
  failure, a Google Drive outage, an AI provider outage, or an account
  suspension by a third party.

Export your data periodically via `Settings → Backup` (JSON, `.ics`, or
`.md`) and keep a copy outside the application.

## 8. Modifications, contributions, forks

OpenTodo is open source. You are free, under the terms of the MIT license,
to fork it, modify it, and redistribute it. If you redistribute a modified
version that meaningfully changes data handling, please ship your own
revised PRIVACY and TERMS documents — these were written for the
upstream open-source distribution.

Contributions are welcomed via pull request; by submitting a contribution
you license it under the MIT license alongside the rest of the project
(see [CONTRIBUTING.md](../CONTRIBUTING.md)).

## 9. Termination

You may stop using OpenTodo at any time by uninstalling the application
and, if you wish, removing the `$APPDATA/OpenTodo/` data folder and
revoking the Google OAuth grant at
[myaccount.google.com/permissions](https://myaccount.google.com/permissions).
There is nothing to cancel and no notice required.

These Terms continue to apply to any copy of the software you retain.

## 10. Limitation of liability

To the maximum extent permitted by applicable law, the OpenTodo
maintainers and contributors shall not be liable for any indirect,
incidental, special, consequential, or punitive damages, or any loss of
profits or revenues, whether incurred directly or indirectly, or any loss
of data, use, goodwill, or other intangible losses, resulting from your
use of (or inability to use) OpenTodo. This limitation is in addition to,
and does not supersede, the disclaimer of warranty in the MIT license.

## 11. Mandatory rights

If the applicable consumer-protection law in your country grants you
mandatory rights that conflict with these Terms, those mandatory rights
prevail.

## 12. Contact

For questions about these Terms, open an issue at
<https://github.com/beanbo/opentodo/issues>.

## 13. Changes to these Terms

Material changes will be reflected by updating the `Last updated` date at
the top of this document and noting the change in the log below.

| Date       | Change                                        |
|------------|-----------------------------------------------|
| 2026-05-17 | Initial template.                             |
| 2026-11-03 | First published version; covers all AI providers and Todoist import; no governing-law clause. |
