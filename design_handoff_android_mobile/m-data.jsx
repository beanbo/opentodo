/*
 * Демо-данные мобильного прототипа OpenTodo.
 * Контент реальный — задачи самого проекта (сбор Android-версии, копирайт-аудит и т.д.).
 */

// short — короткий алиас для вкладки нижней навигации
const M_PROJECTS = [
  { id: 'opentodo', name: 'OpenTodo', short: 'OpenTodo', icon: 'OpenTodoMark', color: '#db4c3f', count: 14 },
  { id: 'print3d',  name: '3д печать', short: '3д печать', icon: 'PrinterIcon',  color: '#88a060', count: 2 },
  { id: 'home',     name: 'Дом и быт', short: 'Дом', icon: 'HeartIcon',    color: '#e0696f', count: 5 },
  { id: 'read',     name: 'Читать / смотреть', short: 'Читать', icon: 'BookIcon', color: '#6c88f4', count: 9 },
];

const M_LABELS = [
  { name: 'баг', color: '#e05050' },
  { name: 'релиз', color: '#eb8909' },
  { name: 'идея', color: '#3d82e0' },
];

// Задачи. due — человекочитаемая строка; dueKind: overdue|today|soon|future|null
const M_TASKS = [
  { id: 't1', text: 'Собрать и протестировать Android-версию', priority: 1, project: 'opentodo', section: 'today', due: 'Сегодня', dueKind: 'today', dueDay: 8, labels: ['релиз'], sub: { done: 1, total: 4 } },
  { id: 't2', text: 'На андроиде приложение моментально крашится при старте', priority: 1, project: 'opentodo', section: 'today', due: 'Сегодня', dueKind: 'today', dueDay: 8, labels: ['баг'], deadline: 'дедлайн сегодня' },
  { id: 't3', text: 'Сделать тач-зоны крупнее — всё мелкое, тяжело попадать пальцем', priority: 2, project: 'opentodo', section: 'today', due: 'Сегодня', dueKind: 'today', dueDay: 8, labels: ['идея'] },
  { id: 't4', text: 'Проверить, не нарушает ли приложение чужие авторские права', priority: 3, project: 'opentodo', section: 'today', due: 'Вчера', dueKind: 'overdue', dueDay: 7 },
  { id: 't5', text: 'Хардкод Google OAuth-ключа — вынести в конфиг', priority: 1, project: 'opentodo', section: 'overdue', due: 'Вчера', dueKind: 'overdue', dueDay: 7, labels: ['баг'] },
  { id: 't6', text: 'Интеграция Claude Code: выполнять задачи прямо из проекта', priority: 2, project: 'opentodo', section: 'upcoming', due: 'Завтра', dueKind: 'soon', dueDay: 9, recurring: true },
  { id: 't7', text: 'Регулярная замена счётчиков воды', priority: 4, project: 'home', section: 'upcoming', due: '15 июня', dueKind: 'future', dueDay: 15, recurring: true },
  { id: 't8', text: 'Нарисовать единый SVG-пак иконок вместо эмодзи', priority: 2, project: 'opentodo', section: 'upcoming', due: '12 июня', dueKind: 'future', dueDay: 12, sub: { done: 6, total: 6 } },
  { id: 't9', text: 'Печатнуть крепёж спинки для стула', priority: 4, project: 'print3d', section: 'upcoming', due: '14 июня', dueKind: 'future', dueDay: 14 },
  { id: 't10', text: 'Купить фотополимерную смолу', priority: 3, project: 'print3d', section: 'upcoming', due: '14 июня', dueKind: 'future', dueDay: 14 },
  { id: 't11', text: 'Дочитать «Designing Interfaces»', priority: 4, project: 'read', section: 'upcoming', due: '20 июня', dueKind: 'future', dueDay: 20 },
];

// Доска (Kanban) — колонки для проекта OpenTodo
const M_BOARD = [
  { id: 'todo', title: 'К выполнению', accent: false, cards: [
    { id: 'b1', text: 'Сделать тач-зоны крупнее', priority: 2, labels: ['идея'], due: 'Сегодня', dueKind: 'today' },
    { id: 'b2', text: 'Нарисовать единый SVG-пак иконок', priority: 2, sub: { done: 6, total: 6 } },
    { id: 'b3', text: 'Регулярная замена счётчиков', priority: 4, recurring: true },
  ]},
  { id: 'doing', title: 'В работе', accent: true, cards: [
    { id: 'b4', text: 'Собрать и протестировать Android-версию', priority: 1, labels: ['релиз'], due: 'Сегодня', dueKind: 'today', sub: { done: 1, total: 4 } },
    { id: 'b5', text: 'Чинить краш при старте на Android', priority: 1, labels: ['баг'] },
  ]},
  { id: 'review', title: 'На проверке', accent: false, cards: [
    { id: 'b6', text: 'Копирайт-аудит интерфейса', priority: 3, due: 'Вчера', dueKind: 'overdue' },
  ]},
  { id: 'done', title: 'Готово', accent: false, cards: [
    { id: 'b7', text: 'Вынести OAuth-ключ в конфиг', priority: 1, done: true },
    { id: 'b8', text: 'Тёмная и светлая темы', priority: 3, done: true },
  ]},
];

// Подзадачи для экрана редактора
const M_SUBTASKS = [
  { id: 's1', text: 'Настроить сборку под arm64', done: true },
  { id: 's2', text: 'Проверить тач-таргеты ≥ 48dp', done: false },
  { id: 's3', text: 'Прогнать на Pixel и компактном телефоне', done: false },
  { id: 's4', text: 'Снять видео для релиза', done: false },
];

// Комментарии/активность в задаче
const M_COMMENTS = [
  { id: 'c1', who: 'Денис', initials: 'Д', text: 'Главный приоритет — крупные тач-зоны и нижняя навигация.', when: 'вчера, 21:14' },
  { id: 'c2', who: 'Claude', initials: 'C', accent: true, text: 'Разбил на 4 подзадачи. Готов взять сборку под arm64 первой.', when: 'сегодня, 09:02' },
];
const M_ACTIVITY = [
  { id: 'a1', text: 'Приоритет изменён на P1', when: 'сегодня, 08:40' },
  { id: 'a2', text: 'Срок установлен на сегодня', when: 'сегодня, 08:39' },
  { id: 'a3', text: 'Задача создана', when: 'вчера, 21:10' },
];

// Корзина
const M_TRASH = [
  { id: 'd1', text: 'Старый прототип нижнего меню', priority: 4, project: 'opentodo', deletedAt: '2 дня назад' },
  { id: 'd2', text: 'Перенести иконки в Figma', priority: 3, project: 'opentodo', deletedAt: '5 дней назад' },
  { id: 'd3', text: 'Купить новый филамент PLA', priority: 4, project: 'print3d', deletedAt: 'неделю назад' },
];

// Уведомления
const M_NOTIFICATIONS = [
  { id: 'n1', kind: 'overdue', icon: 'FlameIcon', color: '#e05050', title: 'Просрочено: 2 задачи', body: 'OAuth-ключ и копирайт-аудит ждут с вчера', when: '5 мин назад', unread: true, taskId: 't5' },
  { id: 'n2', kind: 'reminder', icon: 'AlarmIcon', color: '#eb8909', title: 'Напоминание', body: 'Собрать и протестировать Android-версию', when: '1 ч назад', unread: true, taskId: 't1' },
  { id: 'n3', kind: 'ai', icon: 'SparklesIcon', color: '#e2574b', title: 'Помощник разбил задачу', body: '«Собрать Android-версию» → 4 подзадачи', when: '3 ч назад', taskId: 't1' },
  { id: 'n4', kind: 'done', icon: 'CheckCircleIcon', color: '#4caf50', title: 'Серия продолжается 🔥', body: '7 дней подряд закрываете все задачи дня', when: 'вчера' },
];

// Продуктивность
const M_PRODUCTIVITY = {
  streak: 7, todayDone: 5, todayTotal: 8, weekDone: 34, karma: 8240, level: 'Эксперт',
  week: [
    { d: 'Пн', done: 6, goal: 6 }, { d: 'Вт', done: 4, goal: 6 }, { d: 'Ср', done: 7, goal: 6 },
    { d: 'Чт', done: 5, goal: 6 }, { d: 'Пт', done: 7, goal: 6 }, { d: 'Сб', done: 5, goal: 6 }, { d: 'Вс', done: 5, goal: 6 },
  ],
};

// Сохранённые фильтры
const M_FILTERS = [
  { id: 'f1', name: 'Приоритет 1', query: 'p1', icon: 'FlagIcon', color: '#e05050', count: 3 },
  { id: 'f2', name: 'Назначено мне', query: 'assigned:me', icon: 'TargetIcon', color: '#3d82e0', count: 11 },
  { id: 'f3', name: 'Без срока', query: 'no date', icon: 'CalendarIcon', color: '#9a9a9a', count: 6 },
  { id: 'f4', name: 'С метками', query: '@*', icon: 'TagIcon', color: '#eb8909', count: 8 },
];

const M_REMINDER_OPTS = ['Без напоминания', 'В момент срока', 'За 10 минут', 'За 1 час', 'За день', 'Своё время…'];
const M_REPEAT_OPTS = ['Не повторять', 'Каждый день', 'Каждую неделю', 'Каждые 2 недели', 'Каждый месяц', 'Каждый год', 'Настроить…'];

Object.assign(window, { M_PROJECTS, M_LABELS, M_TASKS, M_BOARD, M_SUBTASKS, M_COMMENTS, M_ACTIVITY, M_TRASH, M_NOTIFICATIONS, M_PRODUCTIVITY, M_FILTERS, M_REMINDER_OPTS, M_REPEAT_OPTS });
