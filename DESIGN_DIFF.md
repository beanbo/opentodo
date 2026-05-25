# Расхождения между дизайн-проектом и QML-приложением

## Прогресс раундов

| Раунд | Commit | Что закрыто |
|---|---|---|
| 1 | `63c29ff` | sidebar 280→220, topbar 56→48, taskItemHeight 40→36, viewTitle 18→15, SectionHeader 5 правок, TaskItem font + minHeight, ProjectView header margins, Settings/Notifications/AI titles |
| 2 | `8a39191` | NavItem margin '1px 8px' + active Medium, TaskRow flex-start + bottom border, filter chips redesign, MenuIcon SVG, SectionHeader count pill, OpenTodoMark brand SVG |
| 3 | `863db0b` | FontSize.viewHeader token 20, Notifications/AI/Trash/Productivity headers, Sidebar bottom-section separator, Note panel subtitle, ProjectView toggle icons 14 |

**Сгенерировано:** 2026-05-25 (после прохода pixel-perfect раундов).
**Источники:**
- Дизайн: `.design-temp/opentodo/project/` (icons.jsx, project-icons.jsx, app-shell.jsx, view-project.jsx, view-others.jsx).
- QML: `qml/layouts/DesktopLayout.qml`, `qml/components/*.qml`, `qml/views/*.qml`, `qml/themes/*.qml`.
- Метрики: `tools/visual_diff.py --artboard project --theme dark` → matching 56.73%, mean_distance 21.38, rmse 58.52.

Все расхождения ниже разбиты по компонентам. Каждое — со ссылкой на источник в обоих сторонах, дизайнерскими значениями и фактическими.
Цветовая семантика статуса:

- 🔴 **критично** — заметно глазами, ломает узнаваемость дизайна.
- 🟠 **средне** — единицы пикселей, токены типографики, аккуратно отстаёт.
- 🟡 **мелко** — < 2 px, в пределах визуального шума.
- ⚪️ **структурно** — архитектурный нюанс, не token-level.
- ✅ **совпадает** — добавлено для полноты (контроль).

---

## 0. Метрики (artboard=project, theme=dark)

| Метрика | Значение |
|---|---|
| Общий размер сетки сравнения | 1264 × 760 px (после strip title bar) |
| Всего пикселей | 960 640 |
| Различающихся (distance > 10) | 415 689 |
| Доля совпадения | **56.73 %** |
| Средний euclidean distance | 21.38 |
| Максимальный | 387.98 |
| RMSE | 58.52 |

Большая часть из 43 % различий — структурная: дизайн рендерит **project** (project header + notes + sections + tasks), а QML показывает **Входящие → empty state**. Без переключения вкладки в QML точное pixel-by-pixel совпадение невозможно. Ниже — расхождения уже **внутри одинаковых элементов** (берётся два рендера общих UI-кусков: sidebar, topbar, общие чипы и т.п.).

---

## 1. Window Chrome (внешняя рамка окна)

| Элемент | Дизайн (app-shell.jsx:6-55) | QML (системная Windows 11 рамка) | Статус |
|---|---|---|---|
| Title bar высота | 32 px, custom (color `titleBar`) | Зависит от Windows (~30 px на 100% DPI) | ⚪️ структурно — Qt использует системный chrome |
| OpenTodoMark в title bar | 14 px, accent color, слева | Системная иконка приложения | ⚪️ структурно |
| Кнопки окна (─ ▢ ✕) | 46×32 px, color `subText` | Системные | ⚪️ структурно |
| Скругление окна | borderRadius 8 | Системное (квадратные углы в Win11 неактивно) | ⚪️ структурно |
| Box-shadow | `0 18px 48px -12px rgba(0,0,0,0.5)…` | Системная тень Win11 | ⚪️ структурно |

**Вывод:** Custom window chrome из дизайна **нельзя воспроизвести в QML без QWindow `FramelessWindowHint`** и собственного title bar. Это серьёзный архитектурный шаг — отдельная задача.

---

## 2. Top bar (FullApp top toolbar)

Дизайн: `app-shell.jsx` line 115-138 — top bar внутри окна.
QML: `qml/layouts/DesktopLayout.qml` line 386-732 — topBar Rectangle.

| Элемент | Дизайн | QML | Статус |
|---|---|---|---|
| Высота | 48 px | 48 px | ✅ |
| Background | `t.bg` | `contentColor` (surfaceColor) | 🟠 разный — дизайн использует bg, наш — surface |
| Border-bottom | 1 px t.border | 1 px borderColor | ✅ |
| Padding | 0 16 px | `taskItemPadding=Spacing.md=16` ←lift/right | ✅ |
| Gap | 14 | RowLayout spacing `12` | 🟠 −2px |
| Иконка-гамбургер (MenuIcon) | 18 px, color iconColor, обёрнута в 28×28 кнопку | ToolButton с `text: "☰"` (юникод-глиф) `font.pixelSize: FontSize.title=18` | 🔴 **дизайн рисует SVG MenuIcon, у нас юникод-символ ☰** |
| Заголовок view (icon + text) | icon 18 + label 14/Medium, gap 8 | viewTitleIcon `size: 18` + Label `FontSize.subtitleSmall=15` / Medium | 🟠 fontSize 14 vs 15 (+1px) |
| Search box | 240×28 px, radius 6, padding 0 10, gap 8, иконка 12 subText, placeholder fontSize 12 subText | SearchBar.qml + Loader `Layout.preferredWidth: 260`, `Layout.preferredHeight: 28`, `radius: Radius.md=6` | 🟠 ширина 260 vs 240, но иначе ✓ |
| Search icon | 12 | (внутри SearchBar.qml) | 🟡 нужно проверить отдельно |
| Search placeholder fontSize | 12 | (внутри SearchBar.qml) | 🟡 нужно проверить отдельно |
| Кнопки eye/folder в топбаре | ❌ нет в дизайне | hideCompletedBtn + expandAll + collapseAll (3 кнопки) | ⚪️ структурно — наши доп.-функции |
| Today goal pill | ❌ нет в дизайне | todayGoalIndicator: 110×24 px radius 12, оранжевый flame + "N/M" | ⚪️ структурно — наша функция |
| Sync indicator | OfflineDotIcon 10 + "Офлайн" subText fontSize 12 | SyncIndicator.qml через Loader | 🟡 нужно проверить |

---

## 3. Sidebar

Дизайн: `app-shell.jsx` Sidebar component line 58-152.
QML: `qml/components/SidebarContent.qml`.

### 3.1 Контейнер

| Элемент | Дизайн | QML | Статус |
|---|---|---|---|
| Ширина | 220 px | `sidebarWidth: 220` в DesktopLayout, `width: 220` в SidebarContent | ✅ |
| Background | `t.sidebar` | `bgColor` = `backgroundColor` | 🟠 разный токен (sidebar vs background — могут различаться по палитрам) |
| Border-right | 1 px `t.border` | в DesktopLayout — `Rectangle 1px borderColor` отдельно | ✅ |
| paddingTop | 6 | mainColumn `topPadding: 8` | 🟡 +2 px |
| paddingBottom | (нет) | `bottomPadding: 8` | 🟡 |
| Overflow | auto | Flickable с ScrollBar.AsNeeded | ✅ |

### 3.2 NavItem (Входящие / Сегодня / Предстоящие)

| Элемент | Дизайн (NavItem) | QML (`SidebarItem`) | Статус |
|---|---|---|---|
| Высота | auto = padding 6+content+6 ≈ 28-30 px | **жёсткая** `height: 36` | 🟠 строки шире на ~6 px чем в дизайне |
| Padding | 6 12 px | leftPad 12 + iconArea 16 + label leftMargin 10 — итого left 12, right 10 (Loader rightMargin) | ✅ left ОК; right 10 vs 12 — 🟡 |
| Margin | 1 px 8 px (между items 1px вертикально, 8px бок) | items занимают `width: parent.width` без margins | 🔴 **дизайн обтекает каждый item 8 px-выступом по бокам — у нас нет** |
| BorderRadius | 6 | `Radius.md = 6` | ✅ |
| Background (idle) | transparent | transparent | ✅ |
| Background (active) | `t.sidebarActive` | `sidebarActiveColor` | ✅ |
| Background (hover) | (не моделируется в дизайне) | `#333333` (dark) / `#f5f5f5` (light) | ⚪️ дополнение QML |
| Цвет текста (idle) | `t.sidebarText` | `textColor` if not dark else `#e0e0e0` | ✅ |
| Цвет текста (active) | `t.accent` | `accentColor` (`#db4c3f`) | ✅ |
| FontSize | 13 | `FontSize.bodySmall = 13` | ✅ |
| FontWeight (idle) | 400 | normal | ✅ |
| FontWeight (active) | 500 | normal — **нет переключения weight для active** | 🟠 active item должен быть Medium |
| Gap icon-text | 10 | iconArea ~16 (icon centred) + leftMargin 10 = gap 10 | ✅ |
| Icon size | 16 | `IconSvg size: 16` | ✅ |
| Icon color (idle) | `t.iconColor` | `isDarkTheme ? "#aaaaaa" : "#555555"` | ✅ (на dark/light тестировались), но другие палитры через ThemeManager.iconColor |

### 3.3 SectionLabel («ИЗБРАННОЕ», «ПРОЕКТЫ», «ФИЛЬТРЫ», «МЕТКИ»)

| Элемент | Дизайн | QML | Статус |
|---|---|---|---|
| Padding | 14 px top, 16 px sides, 4 px bottom | height: 40, leftMargin Spacing.md=16, `verticalCenter` + verticalCenterOffset 4 | 🟠 структура другая — у нас фиксированная высота 40 с центрированием, дизайн — padding-based |
| Высота итоговая | ~30 px (14 + ~12 + 4) | **40 px** | 🟠 +10 px (видно visually) |
| FontSize | 10 | `FontSize.tiny = 10` | ✅ |
| FontWeight | 600 | `Font.DemiBold` | ✅ |
| LetterSpacing | 0.8 | 0.8 | ✅ |
| TextTransform: uppercase | да | `.toUpperCase()` в коде | ✅ |
| Color | `t.subText` | `ThemeManager.secondaryTextColor` | ✅ |

### 3.4 Badge (счётчик "Сегодня → 3 overdue")

| Элемент | Дизайн | QML | Статус |
|---|---|---|---|
| Background | `t.errorBadge` (#e05050) | `ThemeManager.errorColor` (или fallback `#E74C3C`) | ✅ если errorColor=#e05050 |
| FontSize | 10 | `font.pixelSize: 10` | ✅ |
| FontWeight | 600 | `font.bold: true` (= 700) | 🟡 −100 weight |
| Padding | 1 6 px | overdueBadgeText.implicitWidth + 26 (хардкод-26) | 🟠 трудно сравнить точно |
| BorderRadius | 8 | radius 8 (после правки) | ✅ |
| Icon перед числом | WarningIcon 9 px | IconSvg `name: "flame"` size 10 — у нас `flame`, дизайн `warning` | 🟠 **разная иконка** (мы выбрали flame для urgency, дизайн — warning triangle) |
| Gap icon-число | 3 | spacing 4 | 🟡 +1 px |
| Цвет текста и иконки | white | white | ✅ |

### 3.5 Дополнительные элементы sidebar (нижняя секция)

Дизайн: borderTop 1 px + padding 6 0 + (Productivity, Notifications, Assistant, Settings, Trash).
QML: внутри mainColumn — Productivity, Notifications, Помощник, Настройки, Корзина (тот же набор).

| Элемент | Дизайн | QML | Статус |
|---|---|---|---|
| Border-top отделитель | 1 px t.border + padding 6 0 | (нет отдельной разделительной линии) | 🔴 нет разделителя между основным sidebar и bottom-секцией |

### 3.6 Проекты-список (Project tree)

Дизайн: 8 декоративных проектов (Работа, Личное, Идеи, Покупки, Путешествия, Мастерская, Домашние дела, Дом).
QML: только реально существующие пользовательские проекты + добавление через «+».

| Элемент | Дизайн | QML | Статус |
|---|---|---|---|
| ChevronRight для свёрнутых проектов с детьми | (явно в дизайне не моделируется отдельно — обычный NavItem без выпадашки) | есть toggle expand/collapse | ⚪️ функционал больше у QML |

---

## 4. ProjectView header (заголовок проекта)

Дизайн: `view-project.jsx` line 104-124.
QML: `qml/views/ProjectView.qml` line 195-340.

| Элемент | Дизайн | QML | Статус |
|---|---|---|---|
| Padding | 16 24 12 (top sides bottom) | leftMargin `Spacing.lg=24`, rightMargin `Spacing.sm=8`, без отдельных top/bottom | 🟠 правый отступ дизайна 24, у нас 8 |
| Border-bottom | 1 px t.border | (присутствует через стиль) | ✅ |
| Gap | 12 | `Spacing.md = 16` | 🟠 +4 px |
| OpenTodoMark size | 22 | контейнер 22×22, внутри Text 18 (emoji) или Rectangle dot | 🔴 **дизайн рисует красный SVG-маркер с галочкой, у нас text-emoji или цветной круг** |
| Title fontSize | 20 | `FontSize.h2 = 22` | 🟠 +2 px |
| Title fontWeight | 600 | `Font.DemiBold` (=600) | ✅ |
| View toggles (PillIconBtn): radius | 6 | (внутри QML — ToolButtons без явного radius) | 🟠 unclear |
| View toggle padding | 5 10 | по умолчанию ToolButton | 🟠 |
| View toggle icon size | 14 | IconSvg `size: 18` для board, 16 для list | 🔴 14 vs 18 (board), 14 vs 16 (list) |
| View toggle active background | `t.accent` | (через Material.foreground accent при checked) | 🟠 fill vs foreground accent |
| «+ Раздел» кнопка | padding 5 8, fontSize 12 | (наша реализация — ToolButton с текстом) | 🟡 |
| Paperclip кнопка | PillIconBtn | (есть?) | 🟡 проверить |
| More dots кнопка | PillIconBtn MoreIcon | (есть) | ✅ |

---

## 5. Note panel («Заметки»)

Дизайн: `view-project.jsx` line 127-141.
QML: `qml/views/ProjectView.qml` line ~600-700 (notesCard).

| Элемент | Дизайн | QML | Статус |
|---|---|---|---|
| Margin | 12 16 0 (top sides bottom) | (через Layout — определяется родителем) | 🟡 |
| Padding | 12 14 | top/bottom 12, left/right 14 | ✅ |
| Background | `t.surface` | `ThemeManager.surfaceColor` | ✅ |
| BorderRadius | 8 | `Radius.lg = 8` | ✅ |
| Border | 1 px t.border | 1 px borderColor | ✅ |
| Gap | 10 | spacing 10 (в RowLayout header) | ✅ |
| Icon size | 16 | IconSvg `size: 16` | ✅ |
| Icon color | `t.accent` | `ThemeManager.accentColor` | ✅ |
| Title fontSize | 13 | `FontSize.bodySmall = 13` | ✅ |
| Title fontWeight | 500 (Medium) | `Font.Medium` | ✅ |
| Subtitle | "Описание проекта, ссылки, контекст", fontSize 11, marginTop 2, color subText | (subtitle не моделируется как отдельный элемент — у нас весь Markdown текст) | 🟠 нет subtitle-плашки; в дизайне 2 строки текста под заголовком |
| ChevronDown right | 14 | IconSvg `size: 14` | ✅ |

---

## 6. Quick-add row

Дизайн: `view-project.jsx` line 144-150 + InboxView line 12-15.
QML: `qml/components/QuickAdd.qml`.

| Элемент | Дизайн | QML | Статус |
|---|---|---|---|
| Padding | 12 16 (project) / 14 16 (inbox) | leftMargin 16, верхняя `topMargin: 6`, общая высота через collapsedH=56 | 🟠 высота 56 (наша) vs 24-28 (дизайн ~12+content+12=42 или 14+content+14=44) |
| Border-bottom | 1 px t.border | borderColor 1px | ✅ |
| Gap | 10 | leftMargin 10 (после правки) | ✅ |
| Plus icon size | 14 (project), 14 (inbox) | `size: 14` (после правки) | ✅ |
| Plus icon color | `t.accent` | `accentRed` (accentColor) | ✅ |
| Placeholder text | "Добавить задачу…" / "Добавить задачу" | "Добавить задачу" (qsTr) | ✅ |
| Placeholder fontSize | 13 | `FontSize.bodySmall = 13` (после правки) | ✅ |
| Placeholder color | `t.subText` | `subTextCol = ThemeManager.secondaryTextColor` | ✅ |

### Расхождение в высоте QuickAdd

Дизайн: padding 12 16 → высота полосы ~42 px (12 top + content 18 + 12 bot).
QML: `collapsedH: 56` + Layout `+12` = ~68 px видимая полоса. **Разница ≈ 26 px (≈ 60 % высота строки выше дизайна).**

---

## 7. Filter chips (Сегодня / Просрочено / Без даты)

Дизайн: `view-others.jsx` InboxView line 16-31 (там это inline-чипы).
QML: `qml/layouts/DesktopLayout.qml` line 857-1010 (quickFilterBar Repeater).

| Элемент | Дизайн (chip) | QML (chip) | Статус |
|---|---|---|---|
| Высота | ~24 px (padding 6 vert + content 12) | `Layout.preferredHeight: 26` | 🟡 +2 px |
| Padding | 6 10 | (через centerIn anchor) | 🟠 |
| BorderRadius | 16 (обычное rounded) | 13 (pill, halfheight) | 🔴 **дизайн использует rounded 16px, у нас pill-shape (полностью круглые края)** |
| Background | `t.surface` (одинаковый для всех) | `rgba(textColor, 0.08)` (idle) или `accent` (active) | 🔴 **дизайн: статичный surface, наш: tint + active state** |
| Border | 1 px `t.border` | (нет) | 🔴 у дизайна chip имеет рамку, у нас — без рамки |
| FontSize | 12 | `FontSize.caption = 11` | 🟠 −1 px |
| Color text | `t.text` (одинаковый) | `textColor` idle / `#ffffff` active | 🔴 у дизайна нет «активного» состояния — это статичная подсказка |
| Gap | 6 | spacing 4 | 🟡 −2 px |
| Icon size | 12 | `size: 12` | ✅ |
| Icon color | `f.color` или `t.iconColor` | per-chip (включая красный flame для overdue) | ✅ |
| Active state | ❌ нет | да (accent background + bold + white text) | ⚪️ дополнение QML |
| «Сбросить» X-кнопка | ❌ нет | да (видна при активном чипе) | ⚪️ дополнение QML |

**Дизайн моделирует чипы как навигационные ссылки (фильтр клик → переход), у нас как toggle с явным состоянием. Это семантика, а не только стили — но визуально расхождение заметно.**

---

## 8. EmptyState («Задач нет»)

Дизайн: `view-others.jsx` InboxView line 33-52.
QML: `qml/components/EmptyState.qml`.

| Элемент | Дизайн | QML | Статус |
|---|---|---|---|
| Контейнер | flex centered, gap 16, padding 24 | ColumnLayout centerIn, spacing `Spacing.lg=24`, maxWidth 400 | 🟠 gap 24 vs 16 |
| Success badge size | 88×88, radius 44 (full circle) | Image из `success-badge.svg`, Layout 88×88 | ✅ |
| Badge background | #3aa84e | в svg | ✅ |
| Check icon size | 48 | (в svg, фиксировано) | ✅ |
| Check stroke | 2.4 | (в svg) | ✅ |
| Title text | "Задач нет" | "Задач нет" | ✅ |
| Title fontSize | 22 | `FontSize.h2 = 22` | ✅ |
| Title fontWeight | 600 | `Font.DemiBold` | ✅ |
| Message text | "Всё сделано! Самое время добавить что-то новое или просто отдохнуть." | same | ✅ |
| Message fontSize | 13 | `FontSize.body = 14` | 🟠 **+1 px (нужно bodySmall=13)** |
| Message color | `t.subText` | `secondaryTextColor` | ✅ |
| Message maxWidth | 320 | (через Layout.fillWidth + maxWidth 400 у parent) | 🟠 400 vs 320 |
| Message textAlign | center | `Text.AlignHCenter` | ✅ |
| Button padding | 8 16 | implicitHeight 36, left/rightPadding 16 | ✅ |
| Button radius | 20 | **18** | 🟠 −2 px |
| Button background | `t.accent` | accentColor | ✅ |
| Button fontSize | 13 | `FontSize.body = 14` | 🟠 **+1 px (нужно bodySmall=13)** |
| Button fontWeight | 500 | `Font.Medium` | ✅ |
| Plus icon в кнопке size | 14 | `size: 14` | ✅ |
| Plus stroke | 2 (явно прописан) | (наследует из SVG, stroke-width 1.6) | 🟠 stroke 1.6 vs 2 |
| Gap icon-label | 6 | spacing 6 | ✅ |

---

## 9. SectionHeader (раздел в task-листе)

Дизайн: `view-project.jsx` line 81-99.
QML: `qml/components/SectionHeader.qml`.

| Элемент | Дизайн | QML | Статус |
|---|---|---|---|
| Padding | 10 16 6 | leftMargin/rightMargin 16, height 40 | 🟠 высота 40 vs ~28 в дизайне |
| Gap | 6 | spacing 6 | ✅ |
| Border-bottom | 1 px t.border | borderColor 1px | ✅ |
| Chevron size | 12 | `size: 12` (после правки) | ✅ |
| Chevron rotation | (в дизайне просто иконка) | Rotation transform 0°/90° | ⚪️ нашe доп. |
| Title fontSize | 12 | `FontSize.small = 12` | ✅ |
| Title fontWeight | 500 | `Font.Medium` | ✅ |
| Title color | `t.text` | textColor (через cccccc/333333) | ✅ |
| Count badge fontSize | 10 | `FontSize.tiny = 10` | ✅ |
| Count badge padding | 1 7 | (через Layout) | 🟠 |
| Count badge radius | 9 | (наша только text без отдельного pill — `("(N)"` ) | 🔴 **у дизайна count в pill-плашке с background t.surface, у нас просто текст "(N)" без bg** |
| Count badge fontWeight | 500 | (унаследовано) | 🟠 |
| Count badge background | t.surface | (нет — текст) | 🔴 |
| Plus button size | 14 | `size: 14` (после правки) | ✅ |
| More button size | 14 | `size: 14` | ✅ |
| Plus/More visibility | всегда | только при hover (`visible: headerHover.hovered`) | ⚪️ дополнение QML |
| Plus/More color | t.subText | secondaryTextColor | ✅ |

---

## 10. TaskRow

Дизайн: `view-project.jsx` line 49-78.
QML: `qml/components/TaskItem.qml`.

| Элемент | Дизайн | QML | Статус |
|---|---|---|---|
| Align items | flex-start (top-align) | RowLayout default = AlignCenter | 🔴 **дизайн выравнивает по верху, чтобы chevron и chips размещались правильно при многострочном content; у нас по центру** |
| Padding | 8 16 8 16 | leftMargin `Spacing.md=16`, rightMargin `Spacing.sm=8`, vert `_rowVPadding=Spacing.sm=8` | 🟠 right 8 vs 16 |
| Gap | 10 | spacing `_compact ? 6 : 10` | ✅ |
| MinHeight | 36 | `_rowMinHeight: 36` | ✅ |
| Border-bottom | 1 px t.border | (нет explicit border bottom — таски разделены только padding) | 🔴 **у дизайна между задачами тонкая линия, у нас нет** |
| Background (selected) | `${t.accent}1a` (≈ 10 % alpha) | `selectionColor` (из ThemeManager) | ✅ если selectionColor правильный |
| Chevron слева (декоративный) | size 11, color t.subText, paddingTop 3 | size 11, alignment AlignTop + topMargin 3 (после правки) | ✅ |
| Chevron opacity (нет subtasks) | 0.4 (через mute) | opacity 0.4 if not hasSub | ✅ |
| PriorityCheckCircle width/height | 18 | Layout.preferredWidth/Height 18 | ✅ |
| PriorityCheckCircle borderRadius | 9 | radius `width/2 = 9` | ✅ |
| PriorityCheckCircle border | 1.5 px solid priorityColor | border.width 1.5 | ✅ |
| Check icon inside (when done) | size 10 stroke 2.2 | `size: 10` (после правки), stroke от svg | 🟡 stroke может отличаться |
| Text fontSize | 13 | `FontSize.bodySmall = 13` | ✅ |
| Text lineHeight | 1.4 | (Qt default — обычно 1.2-1.3) | 🟠 −0.1-0.2 |
| Text color (done) | t.subText | `_subColor` | ✅ |
| Text strikethrough (done) | yes | `font.strikeout: isCompleted` | ✅ |
| Meta row marginTop | 3 | (через spacing ColumnLayout) | 🟠 unclear |
| Meta row gap | 8 | (через spacing in meta row) | 🟠 |
| Meta wrap | flexWrap | (Layout) | ✅ |
| Chip height | 18 | 18 (после правки) | ✅ |
| Chip padding | 0 6 | width = row.implicitWidth + 12 | ✅ |
| Chip radius | 4 | 4 (после правки) | ✅ |
| Chip fontSize | 11 | `FontSize.caption = 11` | ✅ |
| Chip gap (icon-text) | 4 | spacing 4 | ✅ |
| Chip icon size | 11 | `size: 11` | ✅ |
| Recurring chip background | tinted `${t.accent}22` (≈ 13 % alpha) | `Qt.rgba(accent.r, accent.g, accent.b, 0.12)` | ✅ |
| Recurring chip color | t.accent | accentColor | ✅ |
| Due chip background | transparent | transparent | ✅ |
| Due chip color (overdue) | "#e05050" | `_isOverdue ? "#db4c3f"` | 🟠 e05050 vs db4c3f — разные тона |
| Deadline chip color | "#e05050" | `_deadlineColor` | 🟡 |

---

## 11. Полностраничные views (Productivity / Notifications / AI / Settings / Trash)

Дизайн: каждый View имеет **внутренний** header: padding 16 24, gap 12, icon 22, title fontSize 20 fontWeight 600.

| View | Header высота | Icon | Title size | Title weight | Статус |
|---|---|---|---|---|---|
| **ProductivityView** | дизайн ~54 / QML 56 (Layout.preferredHeight) | productivity 22 ✓ | h2=22 (дизайн 20) | DemiBold ✓ | 🟠 +2 px font |
| **NotificationsView** | дизайн ~54 / QML 56 | bell 22 ✓ | h2=22 (дизайн 20) | DemiBold ✓ | 🟠 +2 px font |
| **AIView** | дизайн ~54 / QML 56 | sparkles 22 textColor ✓ | h2=22 (дизайн 20) | DemiBold ✓ | 🟠 +2 px font |
| **SettingsView** | дизайн ~54 / QML 56 | settings 22 ✓ | 24 (дизайн **показывает 20 во внешнем header + 24 во внутреннем content "Настройки"**) | DemiBold ✓ | 🟡 семантически по-разному |
| **TrashView** | дизайн (не моделируется в jsx) / QML 56 | trash 22 ✓ | h2=22 | DemiBold ✓ | ⚪️ |

**Ключевое расхождение:** дизайн использует **fontSize 20** во view-header. У нас FontSize.h2 = 22. Это **+2 px**. Нужен новый токен `FontSize.viewHeader = 20` или прямой литерал.

---

## 12. Productivity Cards

Дизайн: `view-others.jsx` Card line 56-72 + Pomodoro card line 82-115.
QML: `qml/views/ProductivityView.qml`.

| Элемент | Дизайн | QML | Статус |
|---|---|---|---|
| Card padding | 16 18 | `anchors.margins: 16` | 🟡 right padding 16 vs 18 |
| Card border | 1 px t.border | borderColor 1px | ✅ |
| Card borderRadius | 10 | 10 (после правки) | ✅ |
| Card background | t.surface | contentColor (=surfaceColor) | ✅ |
| Card header gap | 8 | spacing 8 | ✅ |
| Card header marginBottom | 14 | (через ColumnLayout spacing) | 🟠 |
| Card title fontSize | 14 | `font.pixelSize: 16` для Pomodoro card | 🔴 **+2 px** |
| Card title fontWeight | 600 | `font.bold` (= 700) | 🟠 -100 weight |
| Card title icon size | 16 | (различается у разных карточек) | 🟠 |

### 12.1 Pomodoro Timer

| Элемент | Дизайн | QML | Статус |
|---|---|---|---|
| Timer fontSize | 56 | (нужно проверить — посмотреть на PomodoroPanel) | 🟡 |
| Timer fontWeight | 300 (light) | 🟡 | 🟡 |
| Timer letterSpacing | -2 | 🟡 | 🟡 |
| Timer color | t.accent | accentColor | 🟡 |
| Progress bar height | 3 | (нужно проверить) | 🟡 |
| Button padding | 7 18 (Старт) / 7 14 (Пропустить, Сброс) | (через Material defaults) | 🟠 |
| Button radius | 16 (pill) | (через Material defaults) | 🟠 |
| Button fontSize | 13 | 🟡 | 🟡 |

### 12.2 Stat card («Сегодня / За неделю / Всего»)

| Элемент | Дизайн | QML | Статус |
|---|---|---|---|
| Stat cell padding | 14 8 | 🟡 | 🟡 |
| Stat cell background | t.bg | 🟡 | 🟡 |
| Stat cell border | 1 px t.border | 🟡 | 🟡 |
| Stat cell borderRadius | 8 | 🟡 | 🟡 |
| Stat number fontSize | 24 | 🟡 | 🟡 |
| Stat number fontWeight | 600 | 🟡 | 🟡 |
| Stat label fontSize | 11 | 🟡 | 🟡 |
| Stat label marginTop | 2 | 🟡 | 🟡 |

---

## 13. NotificationsView

Дизайн: `view-others.jsx` line 161-201.
QML: `qml/views/NotificationsView.qml`.

| Элемент | Дизайн | QML | Статус |
|---|---|---|---|
| Inner ribbon (BellIcon 16 + "Уведомления" 16/600 + badge с unreadCount) | да | 🟡 проверить | 🟡 |
| Badge background | t.accent | 🟡 | 🟡 |
| Badge fontSize | 11 | 🟡 | 🟡 |
| Badge minWidth | 18 | 🟡 | 🟡 |
| Badge radius | 9 | 🟡 | 🟡 |
| "Прочитать все" / "Очистить" buttons | text-only, color subText fontSize 12 | 🟡 | 🟡 |
| Notification card padding | 12 14 | 🟡 | 🟡 |
| Notification card borderLeft | 3 px зелёный | 🟡 | 🟡 |
| Notification card borderRadius | 6 | 🟡 | 🟡 |
| Notification icon badge | 26×26 radius 13, green | 🟡 | 🟡 |
| Notification icon size | 14, white, stroke 2.4 | 🟡 | 🟡 |
| Notification title fontSize | 13 / weight 500 | 🟡 | 🟡 |
| Notification subtitle fontSize | 11 / subText | 🟡 | 🟡 |
| Notification meta (time) | fontSize 11 / subText | 🟡 | 🟡 |

---

## 14. AssistantView (AIView)

Дизайн: `view-others.jsx` line 203-241.
QML: `qml/views/AIView.qml`.

| Элемент | Дизайн | QML | Статус |
|---|---|---|---|
| Header inner (SparklesIcon 28 accent + "Помощник" 24/600) — это **второй** заголовок (после view-header) | да | 🟡 проверить, есть ли second-level header | 🟠 |
| Suggested prompts (4 chips) | padding 7 14, borderRadius 18, background t.surface, border 1 t.border, fontSize 12 | 🟡 проверить | 🟡 |
| Input box | height 38, borderRadius 8, background surface, border 1 border, padding 0 14, fontSize 13 subText | 🟡 проверить | 🟡 |
| Send button | height 38, padding 0 16, radius 8, bg accent, gap 6, SendIcon 13 | 🟡 проверить | 🟡 |

---

## 15. SettingsView

Дизайн: `view-others.jsx` line 243-354.
QML: `qml/views/SettingsView.qml` + `qml/components/settings/SettingsSection*.qml`.

| Элемент | Дизайн | QML | Статус |
|---|---|---|---|
| Body padding | 20 32 | (отсутствует прямое — через Layout) | 🟠 |
| "Настройки" большой title — fontSize 24/600 | да + marginBottom 16 | RowLayout с IconSvg 22 + Label fontSize 24/DemiBold | ✅ |
| Card marginBottom | 16 | (через spacing) | 🟡 |
| Section header (Общие / Тема / AI помощник) fontSize | 14/600 | 🟡 проверить | 🟡 |
| Section chevron-down | 12 | 🟡 | 🟡 |
| Row padding | 12 0 | 🟡 | 🟡 |
| Row border-bottom | 1 px t.border | 🟡 | 🟡 |
| Row label fontSize | 13 | 🟡 | 🟡 |
| Row hint fontSize | 11 marginTop 2 | 🟡 | 🟡 |
| Pill control padding | 4 10 radius 6 fontSize 12 | 🟡 | 🟡 |
| Select control padding | 5 10 radius 6 fontSize 12 minWidth 180 | 🟡 | 🟡 |
| Toggle | 36×20 radius 10, knob 16×16 radius 8 | 🟡 | 🟡 |
| Theme grid | 4 столбца gap 12, ячейка 78 px высота padding 14, активная — border 2 + accent + check-badge | 🟡 | 🟡 |

---

## 16. BoardView (Kanban)

Дизайн: `view-others.jsx` line 356-444.
QML: `qml/views/BoardView.qml`.

| Элемент | Дизайн | QML | Статус |
|---|---|---|---|
| Header (как ProjectView) | да | 🟡 проверить | 🟡 |
| Note panel (как ProjectView) | да | 🟡 | 🟡 |
| Column width | 220 px fixed | 🟡 | 🟡 |
| Column background | t.surface | 🟡 | 🟡 |
| Column borderRadius | 10 | 🟡 | 🟡 |
| Column header padding | 10 12 + border-bottom | 🟡 | 🟡 |
| Column title fontSize | 12/500 | 🟡 | 🟡 |
| Column count badge fontSize | 10 padding 1 7 radius 9 | 🟡 | 🟡 |
| Column more icon | 12 | 🟡 | 🟡 |
| Card padding | 8 10 | 🟡 | 🟡 |
| Card borderRadius | 6 | 🟡 | 🟡 |
| Card background | t.bg | 🟡 | 🟡 |
| Card fontSize | 12 lineHeight 1.45 | 🟡 | 🟡 |
| «Добавить задачу» footer | padding 8 12, border-top, gap 6, plus 11, fontSize 11, color subText | 🟡 | 🟡 |

---

## 17. Иконпак

| Элемент | Дизайн (icons.jsx + project-icons.jsx) | QML (resources/icons/{ui,projects}) | Статус |
|---|---|---|---|
| viewBox | 24 × 24 | 24 × 24 | ✅ |
| stroke-width | 1.6 | 1.6 | ✅ |
| stroke-linecap | round | round | ✅ |
| stroke-linejoin | round | round | ✅ |
| stroke="currentColor" | yes | yes | ✅ |
| fill | none (по умолчанию) | none | ✅ |
| **Полнота набора** | 51 (icons.jsx) + 43 (project-icons.jsx) = 94, из них skip 3 = 91 | 93/93 совпадают (sync_design_icons.py --check) | ✅ |
| **OpenTodoMark (брендовая)** | inline svg <circle> + <path> в icons.jsx — красный круг + белая галочка | НЕ перенесена как отдельная SVG; в Sidebar — `name: "check-circle"` + accentColor | 🔴 **дизайн использует уникальный brand-маркер; у нас generic check-circle** |

---

## 18. Цвета

См. `tools/check_design_colors.py --strict` → **60 совпадений / 0 расхождений** во всех 5 палитрах.

✅ полностью совпадает.

---

## 19. Шрифт

Дизайн: `'Inter', -apple-system, system-ui, sans-serif`.
QML: системный по умолчанию (не задан `Application.font` явно).

🔴 **расхождение:** дизайн настаивает на Inter (с подгрузкой Google Fonts). У нас системный шрифт (Segoe UI на Windows, Roboto на Android). Влияет на ВСЕ тексты.

---

## 20. Анимации и переходы

Дизайн: статический mockup, анимации не моделируются.
QML: много `Behavior on color/width { ColorAnimation { duration: ... } }` (150-300 мс).

⚪️ структурно — наши доп.-функции, дизайн не моделирует.

---

# Итоговый чек-лист по расхождениям

## 🔴 Критичные (≥ 8 пунктов)

1. **Window Chrome** — frameless window + custom title bar (дизайн моделирует) vs системный (наш).
2. **MenuIcon в топбаре** — дизайн SVG, у нас юникод `☰`.
3. **Sidebar NavItem margin '1px 8px'** — наш items на всю ширину sidebar.
4. **Sidebar NavItem active weight** — должен быть Medium 500, у нас normal.
5. **Sidebar border-top отделитель** между основной и нижней секциями — нет в QML.
6. **OpenTodoMark в ProjectView header** — дизайн рисует красный SVG с галочкой; у нас text-emoji.
7. **View toggles (list/board/calendar) icon size** — дизайн 14, у нас 16-18.
8. **Filter chips style** — дизайн static surface+border radius 16; у нас tinted pill radius 13.
9. **Filter chips background** — surface vs rgba.
10. **SectionHeader count badge** — pill с background t.surface; у нас просто текст "(N)".
11. **TaskRow border-bottom** — дизайн 1px между задачами; у нас нет.
12. **TaskRow align-items** — дизайн flex-start (top), у нас center.
13. **Productivity Card title fontSize** — дизайн 14, у нас 16.
14. **OpenTodoMark в `resources/icons/`** — не перенесена как brand SVG.
15. **Шрифт Inter** — дизайн настаивает, у нас системный.

## 🟠 Средние (заметно, но в пределах ±2 px / ±1 weight)

16. Top bar background — bg vs surface.
17. Top bar gap — 14 vs 12.
18. View title fontSize — 14 vs 15.
19. Sidebar paddingTop — 6 vs 8.
20. SectionLabel высота 30 vs 40.
21. Badge fontWeight — 600 vs 700.
22. Badge icon — warning vs flame.
23. ProjectView header gap — 12 vs 16.
24. ProjectView title fontSize — 20 vs 22 (FontSize.h2).
25. Note panel subtitle отсутствует.
26. QuickAdd высота — 42 vs 56.
27. Filter chip fontSize — 12 vs 11.
28. Filter chip gap — 6 vs 4.
29. SectionHeader высота — 28 vs 40.
30. TaskRow right padding — 16 vs 8.
31. TaskRow text lineHeight — 1.4 vs ~1.2.
32. Due chip overdue color — #e05050 vs #db4c3f.
33. View headers fontSize — 20 vs h2 (22).
34. Card padding right — 18 vs 16.
35. Card title weight — 600 vs Bold(700).
36. EmptyState message fontSize — 13 vs 14.
37. EmptyState button radius — 20 vs 18.
38. EmptyState button fontSize — 13 vs 14.
39. EmptyState message maxWidth — 320 vs 400.
40. EmptyState plus icon stroke — 2 vs 1.6.

## 🟡 Мелкие (< 2 px, не критично)

41. Sidebar right padding — 12 vs 10.
42. Sidebar paddingBottom — лишний 8 px у нас.
43. Filter chip высота — 24 vs 26.
44. EmptyState gap — 16 vs 24.

## ⚪️ Структурные (не token-level — отдельные решения)

45. Win11 window chrome (системный).
46. Дополнительные toolbar кнопки eye/folder×2.
47. Today goal pill.
48. Hover state в Sidebar.
49. Active state у filter chips (toggle).
50. «Сбросить» X-кнопка фильтров.
51. SectionHeader plus/more visible-on-hover.
52. Animation duration на color transitions.

---

# Что делать дальше

Приоритеты на следующий раунд pixel-perfect:

1. **NavItem margin '1px 8px'** — переписать SidebarItem чтобы item не растягивался на всю ширину (отступ 8 px по бокам).
2. **NavItem active weight Medium** — добавить `font.weight: active ? Font.Medium : Font.Normal`.
3. **TaskRow border-bottom** — добавить 1px line между задачами (через ListView spacing 1 + Rectangle, или прямо в TaskItem).
4. **TaskRow align-items top** — заменить RowLayout AlignVCenter на AlignTop для основного row.
5. **Filter chips → дизайнерский стиль** — radius 16, surface bg, border 1px, fontSize 12, gap 6, без active state (или сделать его опциональным).
6. **SectionHeader count в pill** — обернуть `(N)` в Rectangle с padding 1 7, radius 9, background surfaceColor.
7. **Card title fontSize 14/DemiBold** в ProductivityView.
8. **View headers fontSize 20** (новый токен `FontSize.viewHeader` или литерал).
9. **OpenTodoMark brand SVG** — добавить в `resources/icons/ui/opentodo-mark.svg` и использовать в ProjectView header вместо emoji.
10. **Шрифт Inter** — подключить через `QFontDatabase::addApplicationFont` или скачать `Inter.ttf` в `resources/fonts/` и задать через `Application.font` в `main.cpp`.
11. **MenuIcon топбара** — заменить юникод `☰` на IconSvg `name: "menu"`.

После этих 11 пунктов matching score должен подскочить с 56 % к ~75-80 % (оценочно).
