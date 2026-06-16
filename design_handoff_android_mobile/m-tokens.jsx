/*
 * Токены мобильного редизайна OpenTodo.
 * 9 палитр из ThemeManager.qml + M3-производные поля (тональные контейнеры,
 * поверхность нав-бара). Плотность — «просторно»/«сбалансировано».
 */

function mkDark(o) {
  return Object.assign({
    isDark: true, statusDark: true,
    surface2: o.surface, elevated: o.card, hairline: o.border,
    accentContainer: o.accentContainer, accentOn: o.accentOn,
    navBar: o.navBar || o.surface, scrim: 'rgba(0,0,0,0.55)',
    error: '#e05050', success: '#4caf50', warning: '#eb9b18',
    p1: '#e05050', p2: '#eb8909', p3: '#3d82e0', p4: '#7d7d7d',
  }, o);
}

const M_THEMES = {
  classic_light: {
    isDark: false, statusDark: false,
    bg: '#f6f6f4', surface: '#ffffff', surface2: '#fbfbf9', card: '#ffffff', elevated: '#ffffff',
    text: '#1d1d1d', sub: '#7a7a78', faint: '#a6a6a2', border: '#e7e7e2', hairline: '#efefea', hover: '#f2f1ed',
    accent: '#db4c3f', accentFg: '#ffffff', accentContainer: '#ffe7e2', accentOn: '#b53527',
    navBar: '#ffffff', scrim: 'rgba(0,0,0,0.32)',
    error: '#d8463a', success: '#3aa84e', warning: '#d98a10',
    p1: '#db4c3f', p2: '#e08410', p3: '#2070d8', p4: '#b0b0ac',
  },
  dark: {
    isDark: true, statusDark: true,
    bg: '#1f1f1f', surface: '#282828', surface2: '#2e2e2e', card: '#303030', elevated: '#323232',
    text: '#ededed', sub: '#9a9a9a', faint: '#6f6f6f', border: '#363636', hairline: '#2e2e2e', hover: '#353535',
    accent: '#e2574b', accentFg: '#ffffff', accentContainer: '#3a2320', accentOn: '#f0a59c',
    navBar: '#242424', scrim: 'rgba(0,0,0,0.55)',
    error: '#e05050', success: '#4caf50', warning: '#eb9b18',
    p1: '#e05050', p2: '#eb8909', p3: '#3d82e0', p4: '#7d7d7d',
  },
  kale: mkDark({
    bg: '#3b4a3e', surface: '#344438', surface2: '#3a4b3e', card: '#3a4f3d', elevated: '#3d5340',
    text: '#d8e8d6', sub: '#8ea88c', faint: '#6f8a6d', border: '#4a5e4d', hairline: '#3f5142', hover: '#435643',
    accent: '#7fba7a', accentFg: '#ffffff', accentContainer: '#2f4433', accentOn: '#a9d6a4',
    navBar: '#2f3c33', p4: '#8ea88c',
  }),
  blueberry: mkDark({
    bg: '#1a1d3b', surface: '#1e2145', surface2: '#20234a', card: '#222550', elevated: '#24285a',
    text: '#d0d4f0', sub: '#8a90c8', faint: '#6b71a8', border: '#2a2e5e', hairline: '#24284f', hover: '#282b58',
    accent: '#6c88f4', accentFg: '#ffffff', accentContainer: '#232a55', accentOn: '#9aa8f8',
    navBar: '#171a3a', p3: '#5a8aff', p4: '#8a90c8',
  }),
  lavender: mkDark({
    bg: '#2d2042', surface: '#332648', surface2: '#37294e', card: '#352a4e', elevated: '#3a2c52',
    text: '#dcd0e8', sub: '#a090b8', faint: '#806a98', border: '#3e2e58', hairline: '#382a52', hover: '#3a3055',
    accent: '#b07fd0', accentFg: '#ffffff', accentContainer: '#3a2c55', accentOn: '#c59ae0',
    navBar: '#271b3a', p4: '#a090b8',
  }),
  raspberry: mkDark({
    bg: '#3b1a2d', surface: '#421e33', surface2: '#471f36', card: '#481e38', elevated: '#4a2138',
    text: '#e8d0dc', sub: '#a88098', faint: '#8a6678', border: '#502840', hairline: '#45203a', hover: '#4e2540',
    accent: '#e06088', accentFg: '#ffffff', accentContainer: '#4a2440', accentOn: '#ec85a4',
    navBar: '#30152a', p4: '#a88098',
  }),
  graphite: mkDark({
    bg: '#282828', surface: '#2e2e2e', surface2: '#313131', card: '#333333', elevated: '#353535',
    text: '#d0d0d0', sub: '#808080', faint: '#6a6a6a', border: '#3a3a3a', hairline: '#333333', hover: '#383838',
    accent: '#7a9cc6', accentFg: '#ffffff', accentContainer: '#2c3540', accentOn: '#9ab6d8',
    navBar: '#232323', p3: '#7a9cc6', p4: '#808080',
  }),
  sunflower: {
    isDark: false, statusDark: false,
    bg: '#fffbf0', surface: '#ffffff', surface2: '#fffaf0', card: '#fff8e8', elevated: '#ffffff',
    text: '#302010', sub: '#806040', faint: '#a89060', border: '#e8e0c8', hairline: '#f0e8d2', hover: '#f8f2e2',
    accent: '#e8a820', accentFg: '#202020', accentContainer: '#fff0d0', accentOn: '#b5851a',
    navBar: '#fffdf5', scrim: 'rgba(0,0,0,0.35)',
    error: '#d33b2c', success: '#3aa84e', warning: '#d98a10',
    p1: '#db4c3f', p2: '#e08410', p3: '#2070d8', p4: '#a89060',
  },
  highcontrast: {
    isDark: true, statusDark: true,
    bg: '#000000', surface: '#0d0d0d', surface2: '#0d0d0d', card: '#0d0d0d', elevated: '#141414',
    text: '#ffffff', sub: '#ffff00', faint: '#cccc00', border: '#ffffff', hairline: '#444444', hover: '#1a1a1a',
    accent: '#ffff00', accentFg: '#000000', accentContainer: '#1a1a00', accentOn: '#ffff66',
    navBar: '#000000', scrim: 'rgba(0,0,0,0.72)',
    error: '#ff6060', success: '#00ff7f', warning: '#ffa000',
    p1: '#ff4040', p2: '#ffa000', p3: '#00bfff', p4: '#ffffff',
  },
};
// Совместимость со старым кодом
M_THEMES.light = M_THEMES.classic_light;

// Порядок и подписи для экрана настроек
const M_THEME_LIST = [
  { key: 'classic_light', label: 'Светлая' },
  { key: 'dark', label: 'Тёмная' },
  { key: 'graphite', label: 'Графит' },
  { key: 'kale', label: 'Капуста' },
  { key: 'blueberry', label: 'Черника' },
  { key: 'lavender', label: 'Лаванда' },
  { key: 'raspberry', label: 'Малина' },
  { key: 'sunflower', label: 'Подсолнух' },
  { key: 'highcontrast', label: 'Контраст' },
];

// Палитра кастомных акцентов (M3 dynamic color)
const M_ACCENTS = ['#db4c3f', '#e2574b', '#e8702a', '#eaa72a', '#3aa84e', '#2c9c8e', '#2070d8', '#6c88f4', '#9b5de5', '#e0608a'];

// Плотность списка — «просторно» по умолчанию (крупные тач-зоны).
const M_DENSITY = {
  spacious: {
    rowPadV: 15, rowPadH: 18, rowGap: 14, rowMinH: 66,
    check: 24, checkInner: 13, title: 16.5, titleLh: 1.32, meta: 12.5,
    chipH: 23, sectionH: 46, sectionFs: 13, listTopGap: 4,
  },
  balanced: {
    rowPadV: 11, rowPadH: 16, rowGap: 12, rowMinH: 54,
    check: 22, checkInner: 12, title: 15, titleLh: 1.3, meta: 12,
    chipH: 21, sectionH: 40, sectionFs: 12.5, listTopGap: 2,
  },
};

// Применить кастомный акцент к палитре (возвращает новый объект темы)
function applyAccent(t, accent) {
  if (!accent || accent === t.accent) return t;
  return Object.assign({}, t, {
    accent,
    accentFg: t.isDark ? '#ffffff' : (accent === '#eaa72a' || accent === '#e8702a' ? '#202020' : '#ffffff'),
    accentContainer: (t.isDark ? accent + '2e' : accent + '24'),
    accentOn: accent,
  });
}

const M_PRIORITY_COLOR = (t, p) => ({ 1: t.p1, 2: t.p2, 3: t.p3, 4: t.p4 }[p] || t.p4);

const M_FONT = "'Inter', Roboto, -apple-system, system-ui, sans-serif";

Object.assign(window, { M_THEMES, M_THEME_LIST, M_ACCENTS, M_DENSITY, M_PRIORITY_COLOR, M_FONT, applyAccent });
