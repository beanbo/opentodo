/*
 * OpenTodo icon pack — единый stroke-стиль.
 *
 * Правила:
 *  - viewBox 24×24, центрированы
 *  - stroke="currentColor", strokeWidth от пропа (default 1.6)
 *  - strokeLinecap="round" strokeLinejoin="round"
 *  - fill="none" по умолчанию; некоторые «filled» состояния задают fill
 *  - Размер задаётся пропом size — рендерится квадратной svg
 *
 * Это компактное замена для emoji + текущих SVG. Чтобы перенести в Qt,
 * каждую иконку можно сохранить в resources/icons/ui/<name>.svg
 * с stroke="currentColor" (как и текущая IconSvg.qml ожидает).
 */

const Icon = ({ d, paths, children, size = 18, stroke = 1.6, fill, color, style }) => (
  <svg
    width={size}
    height={size}
    viewBox="0 0 24 24"
    fill={fill || 'none'}
    stroke="currentColor"
    strokeWidth={stroke}
    strokeLinecap="round"
    strokeLinejoin="round"
    style={{ color, flexShrink: 0, ...style }}
  >
    {d && <path d={d} />}
    {paths && paths.map((p, i) => <path key={i} {...p} />)}
    {children}
  </svg>
);

// ─── Sidebar / навигация ──────────────────────────────────────────────────
const InboxIcon = (p) => (
  <Icon {...p}>
    <path d="M3 13l2.5-7A2 2 0 0 1 7.4 4.7h9.2A2 2 0 0 1 18.5 6L21 13" />
    <path d="M3 13v5a2 2 0 0 0 2 2h14a2 2 0 0 0 2-2v-5h-5a3 3 0 0 1-6 0H3z" />
  </Icon>
);

const TodayIcon = (p) => (
  <Icon {...p}>
    <rect x="3.5" y="5" width="17" height="15" rx="2" />
    <path d="M3.5 10h17" />
    <path d="M8 3v4M16 3v4" />
    <circle cx="12" cy="15" r="1.4" fill="currentColor" stroke="none" />
  </Icon>
);

const UpcomingIcon = (p) => (
  <Icon {...p}>
    <rect x="3.5" y="5" width="17" height="15" rx="2" />
    <path d="M3.5 10h17" />
    <path d="M8 3v4M16 3v4" />
    <path d="M8 14h2M13 14h3M8 17h5" />
  </Icon>
);

const ProductivityIcon = (p) => (
  <Icon {...p}>
    <path d="M4 20V10M10 20V4M16 20v-7M22 20H2" />
  </Icon>
);

const BellIcon = (p) => (
  <Icon {...p}>
    <path d="M6 9a6 6 0 0 1 12 0c0 4 1.5 5.5 2 6.5H4c.5-1 2-2.5 2-6.5z" />
    <path d="M10 19a2 2 0 0 0 4 0" />
  </Icon>
);

const SparklesIcon = (p) => (
  <Icon {...p}>
    <path d="M12 3l1.5 4.5L18 9l-4.5 1.5L12 15l-1.5-4.5L6 9l4.5-1.5L12 3z" />
    <path d="M18.5 15l.7 2 2 .8-2 .7-.7 2-.8-2-2-.7 2-.8.8-2z" />
  </Icon>
);

const SettingsIcon = (p) => (
  <Icon {...p}>
    <circle cx="12" cy="12" r="3" />
    <path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 1 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-4 0v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 1 1-2.83-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1 0-4h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 1 1 2.83-2.83l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 1 1 2.83 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1z" />
  </Icon>
);

const TrashIcon = (p) => (
  <Icon {...p}>
    <path d="M4 7h16" />
    <path d="M9 7V4.5A1.5 1.5 0 0 1 10.5 3h3A1.5 1.5 0 0 1 15 4.5V7" />
    <path d="M6 7l1 12.5A1.5 1.5 0 0 0 8.5 21h7a1.5 1.5 0 0 0 1.5-1.5L18 7" />
    <path d="M10 11v6M14 11v6" />
  </Icon>
);

// ─── Проекты / разделы ────────────────────────────────────────────────────
const HashIcon = (p) => (
  <Icon {...p}>
    <path d="M5 9h14M5 15h14M10 4l-2 16M16 4l-2 16" />
  </Icon>
);

const FolderIcon = (p) => (
  <Icon {...p}>
    <path d="M3 6.5A1.5 1.5 0 0 1 4.5 5h4.4a1.5 1.5 0 0 1 1.2.6L11.5 7H19.5A1.5 1.5 0 0 1 21 8.5v9A1.5 1.5 0 0 1 19.5 19h-15A1.5 1.5 0 0 1 3 17.5v-11z" />
  </Icon>
);

const ProjectCubeIcon = (p) => (
  <Icon {...p}>
    <path d="M12 3l8 4.5v9L12 21l-8-4.5v-9L12 3z" />
    <path d="M4 7.5L12 12l8-4.5M12 12v9" />
  </Icon>
);

const StarIcon = (p) => (
  <Icon {...p}>
    <path d="M12 3l2.7 5.5 6.1.9-4.4 4.3 1 6.1L12 17l-5.4 2.8 1-6.1L3.2 9.4l6.1-.9L12 3z" />
  </Icon>
);

const TagIcon = (p) => (
  <Icon {...p}>
    <path d="M20.6 12.6l-8-8A2 2 0 0 0 11.2 4H5a1 1 0 0 0-1 1v6.2a2 2 0 0 0 .6 1.4l8 8a2 2 0 0 0 2.8 0l5.2-5.2a2 2 0 0 0 0-2.8z" />
    <circle cx="8.5" cy="8.5" r="1.2" fill="currentColor" stroke="none" />
  </Icon>
);

const FilterIcon = (p) => (
  <Icon {...p}>
    <path d="M3 5h18l-7 9v6l-4-2v-4L3 5z" />
  </Icon>
);

// ─── Состояния задач ──────────────────────────────────────────────────────
const CheckIcon = (p) => (
  <Icon {...p}>
    <path d="M5 12.5l4.5 4.5L19 7" />
  </Icon>
);

const CheckCircleIcon = (p) => (
  <Icon {...p}>
    <circle cx="12" cy="12" r="9" />
    <path d="M8 12.5l2.8 2.8L16 10" />
  </Icon>
);

const CircleIcon = (p) => (
  <Icon {...p}>
    <circle cx="12" cy="12" r="9" />
  </Icon>
);

const ChevronRightIcon = (p) => (
  <Icon {...p}>
    <path d="M9 5l7 7-7 7" />
  </Icon>
);

const ChevronDownIcon = (p) => (
  <Icon {...p}>
    <path d="M5 9l7 7 7-7" />
  </Icon>
);

const PlusIcon = (p) => (
  <Icon {...p}>
    <path d="M12 5v14M5 12h14" />
  </Icon>
);

const MoreIcon = (p) => (
  <Icon {...p}>
    <circle cx="5" cy="12" r="1.2" fill="currentColor" stroke="none" />
    <circle cx="12" cy="12" r="1.2" fill="currentColor" stroke="none" />
    <circle cx="19" cy="12" r="1.2" fill="currentColor" stroke="none" />
  </Icon>
);

const PinIcon = (p) => (
  <Icon {...p}>
    <path d="M16 3l5 5-3 1-3.5 3.5L15 16l-4-4-5 5 5-5-4-4 3.5-.5L11 4l5-1z" />
  </Icon>
);

const SearchIcon = (p) => (
  <Icon {...p}>
    <circle cx="11" cy="11" r="6.5" />
    <path d="M16 16l4 4" />
  </Icon>
);

const LinkIcon = (p) => (
  <Icon {...p}>
    <path d="M10.5 13.5a3.5 3.5 0 0 0 5 0l3-3a3.5 3.5 0 0 0-5-5l-1.5 1.5" />
    <path d="M13.5 10.5a3.5 3.5 0 0 0-5 0l-3 3a3.5 3.5 0 0 0 5 5l1.5-1.5" />
  </Icon>
);

const PaperclipIcon = (p) => (
  <Icon {...p}>
    <path d="M21 11l-9 9a5 5 0 0 1-7-7l9-9a3.5 3.5 0 0 1 5 5l-9 9a2 2 0 0 1-3-3l8-8" />
  </Icon>
);

// ─── Время ────────────────────────────────────────────────────────────────
const ClockIcon = (p) => (
  <Icon {...p}>
    <circle cx="12" cy="12" r="9" />
    <path d="M12 7v5l3 2" />
  </Icon>
);

const AlarmIcon = (p) => (
  <Icon {...p}>
    <circle cx="12" cy="13" r="7.5" />
    <path d="M12 8.5V13l2.5 1.5" />
    <path d="M4 6l3-3M20 6l-3-3" />
  </Icon>
);

const RepeatIcon = (p) => (
  <Icon {...p}>
    <path d="M17 3l3 3-3 3" />
    <path d="M20 6H8a4 4 0 0 0-4 4v1" />
    <path d="M7 21l-3-3 3-3" />
    <path d="M4 18h12a4 4 0 0 0 4-4v-1" />
  </Icon>
);

const CalendarIcon = (p) => (
  <Icon {...p}>
    <rect x="3.5" y="5" width="17" height="15" rx="2" />
    <path d="M3.5 10h17" />
    <path d="M8 3v4M16 3v4" />
  </Icon>
);

// ─── Виды списка ──────────────────────────────────────────────────────────
const ListIcon = (p) => (
  <Icon {...p}>
    <path d="M8 6h12M8 12h12M8 18h12" />
    <circle cx="4" cy="6" r="1" fill="currentColor" stroke="none" />
    <circle cx="4" cy="12" r="1" fill="currentColor" stroke="none" />
    <circle cx="4" cy="18" r="1" fill="currentColor" stroke="none" />
  </Icon>
);

const BoardIcon = (p) => (
  <Icon {...p}>
    <rect x="3.5" y="4" width="5" height="16" rx="1.5" />
    <rect x="10" y="4" width="5" height="10" rx="1.5" />
    <rect x="16.5" y="4" width="4" height="13" rx="1.5" />
  </Icon>
);

const CalendarViewIcon = CalendarIcon;

// ─── Меню / окно ──────────────────────────────────────────────────────────
const MenuIcon = (p) => (
  <Icon {...p}>
    <path d="M4 7h16M4 12h16M4 17h16" />
  </Icon>
);

const SidebarIcon = (p) => (
  <Icon {...p}>
    <rect x="3.5" y="4.5" width="17" height="15" rx="2" />
    <path d="M10 4.5v15" />
  </Icon>
);

const NoteIcon = (p) => (
  <Icon {...p}>
    <path d="M5 4a1 1 0 0 1 1-1h9l4 4v13a1 1 0 0 1-1 1H6a1 1 0 0 1-1-1V4z" />
    <path d="M15 3v4h4" />
    <path d="M8 12h8M8 16h5" />
  </Icon>
);

// ─── Декоративные иконки проектов (вместо случайных эмодзи) ───────────────
const RocketIcon = (p) => (
  <Icon {...p}>
    <path d="M5 19c-1 1-1 3-1 3s2 0 3-1" />
    <path d="M14.5 4.5C18 4 21 7 20.5 10.5L12 19l-5-5L14.5 4.5z" />
    <path d="M9 13l-3 1-2 2 3 1 1 3 2-2 1-3" />
    <circle cx="15" cy="9" r="1.5" />
  </Icon>
);

const RunnerIcon = (p) => (
  <Icon {...p}>
    <circle cx="15" cy="4.5" r="1.5" />
    <path d="M14 8l-3 3 2 3v5" />
    <path d="M9 11l-4 2M13 14l4 1 2 4" />
    <path d="M8 19l3-5" />
  </Icon>
);

const PaletteIcon = (p) => (
  <Icon {...p}>
    <path d="M12 3a9 9 0 1 0 1 17.9c1.5-.2 2-1.8 1.2-3-.7-1 0-2.4 1.2-2.4H18a3 3 0 0 0 3-3 9 9 0 0 0-9-9.5z" />
    <circle cx="7.5" cy="11" r="1" fill="currentColor" stroke="none" />
    <circle cx="9" cy="7" r="1" fill="currentColor" stroke="none" />
    <circle cx="13.5" cy="6" r="1" fill="currentColor" stroke="none" />
    <circle cx="17" cy="9" r="1" fill="currentColor" stroke="none" />
  </Icon>
);

const TargetIcon = (p) => (
  <Icon {...p}>
    <circle cx="12" cy="12" r="9" />
    <circle cx="12" cy="12" r="5.5" />
    <circle cx="12" cy="12" r="2" fill="currentColor" stroke="none" />
  </Icon>
);

const HeartIcon = (p) => (
  <Icon {...p}>
    <path d="M12 20s-7-4.5-7-10a4 4 0 0 1 7-2.5A4 4 0 0 1 19 10c0 5.5-7 10-7 10z" />
  </Icon>
);

const BookIcon = (p) => (
  <Icon {...p}>
    <path d="M4 5a2 2 0 0 1 2-2h13v16H6a2 2 0 0 0-2 2V5z" />
    <path d="M4 19a2 2 0 0 1 2-2h13" />
    <path d="M8 7h7M8 11h5" />
  </Icon>
);

const PrinterIcon = (p) => (
  <Icon {...p}>
    <path d="M7 9V3h10v6" />
    <rect x="4" y="9" width="16" height="8" rx="1.5" />
    <rect x="7" y="14" width="10" height="7" />
    <circle cx="17" cy="12" r=".8" fill="currentColor" stroke="none" />
  </Icon>
);

// ─── Доп. ─────────────────────────────────────────────────────────────────
const WarningIcon = (p) => (
  <Icon {...p}>
    <path d="M12 3l10 17H2L12 3z" />
    <path d="M12 10v4" />
    <circle cx="12" cy="17" r=".9" fill="currentColor" stroke="none" />
  </Icon>
);

const FlameIcon = (p) => (
  <Icon {...p}>
    <path d="M12 3s-1 3-3 5-3 4-3 7a6 6 0 0 0 12 0c0-3-2-5-3-7s-3-5-3-5z" />
    <path d="M12 13s-2 2-2 4a2 2 0 0 0 4 0c0-1.5-2-4-2-4z" />
  </Icon>
);

const FlagIcon = (p) => (
  <Icon {...p}>
    <path d="M5 21V4" />
    <path d="M5 4h11l-1.5 4L16 12H5" />
  </Icon>
);

const PlayIcon = (p) => (
  <Icon {...p}>
    <path d="M7 4l13 8-13 8V4z" fill="currentColor" />
  </Icon>
);

const PauseIcon = (p) => (
  <Icon {...p}>
    <rect x="6" y="4" width="4" height="16" rx="1" fill="currentColor" stroke="none" />
    <rect x="14" y="4" width="4" height="16" rx="1" fill="currentColor" stroke="none" />
  </Icon>
);

const SkipIcon = (p) => (
  <Icon {...p}>
    <path d="M5 5l9 7-9 7V5z" fill="currentColor" />
    <path d="M18 5v14" />
  </Icon>
);

const ResetIcon = (p) => (
  <Icon {...p}>
    <path d="M20 12a8 8 0 1 1-3-6.3" />
    <path d="M20 4v5h-5" />
  </Icon>
);

const SyncIcon = (p) => (
  <Icon {...p}>
    <path d="M3 12a9 9 0 0 1 15-6.7L21 8" />
    <path d="M21 4v4h-4" />
    <path d="M21 12a9 9 0 0 1-15 6.7L3 16" />
    <path d="M3 20v-4h4" />
  </Icon>
);

const OfflineDotIcon = (p) => (
  <Icon {...p}>
    <circle cx="12" cy="12" r="4" fill="currentColor" stroke="none" />
  </Icon>
);

const SendIcon = (p) => (
  <Icon {...p}>
    <path d="M3 12l18-8-7 18-3-8-8-2z" />
  </Icon>
);

// Маркер OpenTodo — фирменный знак: круг с галочкой-стрелкой
const OpenTodoMark = ({ size = 18, color = '#db4c3f' }) => (
  <svg width={size} height={size} viewBox="0 0 24 24" fill="none" style={{ flexShrink: 0 }}>
    <circle cx="12" cy="12" r="9.5" fill={color} />
    <path d="M7 12.5l3.2 3.2L17 9" stroke="white" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" fill="none" />
  </svg>
);

Object.assign(window, {
  Icon,
  InboxIcon, TodayIcon, UpcomingIcon, ProductivityIcon, BellIcon, SparklesIcon, SettingsIcon, TrashIcon,
  HashIcon, FolderIcon, ProjectCubeIcon, StarIcon, TagIcon, FilterIcon,
  CheckIcon, CheckCircleIcon, CircleIcon, ChevronRightIcon, ChevronDownIcon, PlusIcon, MoreIcon, PinIcon, SearchIcon, LinkIcon, PaperclipIcon,
  ClockIcon, AlarmIcon, RepeatIcon, CalendarIcon,
  ListIcon, BoardIcon, CalendarViewIcon,
  MenuIcon, SidebarIcon, NoteIcon,
  RocketIcon, RunnerIcon, PaletteIcon, TargetIcon, HeartIcon, BookIcon, PrinterIcon,
  WarningIcon, FlameIcon, FlagIcon, PlayIcon, PauseIcon, SkipIcon, ResetIcon, SyncIcon, OfflineDotIcon, SendIcon,
  OpenTodoMark,
});
