# OpenTodo

[![CI](https://github.com/USER/opentodo/actions/workflows/ci.yml/badge.svg)](https://github.com/USER/opentodo/actions/workflows/ci.yml)
[![Release](https://github.com/USER/opentodo/actions/workflows/release.yml/badge.svg)](https://github.com/USER/opentodo/actions/workflows/release.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

Кросс-платформенный планировщик задач на Qt 6 / QML / C++20. Бесплатная альтернатива Todoist с локальным хранением и Google Drive-синхронизацией.

## Возможности

- 📋 Проекты с иерархией, разделы, подзадачи (без ограничения глубины)
- 🏷 Метки, фильтры с собственным DSL (`today & p1 | #Work`)
- 🔄 Естественный язык для дат на русском и английском (`завтра в 9`, `каждый понедельник`)
- ☁️ Синхронизация через Google Drive с CRDT-разрешением конфликтов (HLC + per-field LWW)
- 🔔 Системные уведомления (Windows toast, Android notifications)
- 🎯 Pomodoro-таймер, ежедневный обзор, статистика и карма
- 🤖 AI-помощник для разбиения задач на подзадачи (Anthropic Claude API)
- 🎨 8 тем оформления, светлая/тёмная, узкий режим
- ⌨️ Глобальная горячая клавиша для быстрого добавления (Ctrl+Alt+A)
- 🌐 Поддержка платформ: Windows, Linux, macOS (десктоп) · Android · Web (WASM)

## Скриншот

_(добавьте скриншот сюда после первой публикации)_

## Сборка

Требуется Qt 6.11.0 с модулями: `Core Gui Quick Qml Network Widgets QuickControls2`. Опционально `Positioning` для гео-напоминаний.

### Windows / Linux / macOS

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build         # запустить тесты
```

### Android

См. [docs/ANDROID.md](docs/ANDROID.md). Кратко:

```bash
export ANDROID_SDK_ROOT=/path/to/sdk
export ANDROID_NDK_ROOT=/path/to/ndk
cmake -S . -B build-android -DCMAKE_TOOLCHAIN_FILE=$Qt6_DIR/lib/cmake/Qt6/qt.toolchain.cmake \
      -DQT_ANDROID_ABIS=arm64-v8a
cmake --build build-android
```

### Web (WASM)

См. [docs/WASM.md](docs/WASM.md) и [tools/build-wasm.bat](tools/build-wasm.bat).

## Архитектура

Layered design в `src/`:

- **model/** — POJO Q_OBJECT: Task, Project, Section, Label, Filter, Reminder, Comment
- **storage/** — DataStore (in-memory), FileManager (atomic JSON persistence)
- **sync/** — SyncEngine (commit log), HLC (hybrid logical clock), Merger (CRDT), GDriveClient
- **filter/** — Собственный DSL парсер с AST и evaluator
- **nlp/** — Парсер дат (RU/EN), QuickAdd, RecurrenceEngine
- **viewmodels/** — One per UI concern (Task, Project, Sidebar, Filter, Search, Karma, ...)
- **services/** — Notifications (Win/Android/WASM/stub), Hotkey, Reminders, Pomodoro, AIAssistant

QML в `qml/` — `OpenTodoUI` модуль, точка входа `Main.qml`.

Подробная архитектурная справка с диаграммами слоёв и потока синхронизации: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md). Канонический справочник для разработчиков: [CLAUDE.md](CLAUDE.md).

## Поддержка разработки

Если проект оказался полезен — поддержать развитие можно криптовалютой. Адреса смотрите в `Настройки → Поддержать разработчика`.

## Конфиденциальность и условия использования

OpenTodo хранит все данные локально; облачная синхронизация, AI-интеграция и анонимная телеметрия — строго опциональные функции с отключённым состоянием по умолчанию.

- [docs/PRIVACY.md](docs/PRIVACY.md) — что передаётся при включении синхронизации, AI и телеметрии, и что **не** собирается никогда.
- [docs/TERMS.md](docs/TERMS.md) — условия использования (open-source дистрибутив, без аккаунтов и подписок).

Документы выложены как **шаблоны**: поля, помеченные `<TODO>` (email мейнтейнера, юрисдикция и т.п.), должны быть заполнены перед публикацией вашей сборки. Полный индекс документации — [docs/README.md](docs/README.md).

## Лицензия

OpenTodo распространяется по лицензии [MIT](LICENSE).

Сторонние компоненты (Qt 6 под LGPLv3, zlib, OpenSSL и др.) и условия их использования перечислены в файле [NOTICE](NOTICE). При распространении сборок OpenTodo необходимо сопровождать их обоими файлами — `LICENSE` и `NOTICE`.
