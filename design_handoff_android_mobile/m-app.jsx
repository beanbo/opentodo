/*
 * Оболочка мобильного OpenTodo: роутинг между экранами, варианты нижней навигации,
 * FAB и оверлеи (drawer / редактор / quick-add / поиск).
 */
const { useState: useStateA, useEffect: useEffectA, useRef: useRefA } = React;

// ── Нижняя навигация (3 варианта) ─────────────────────────────────────────
function NavItem({ icon: I, label, active, t, onClick, onLongPress, dim }) {
  const lp = useRefA(null);
  const fired = useRefA(false);
  const startLP = () => {
    fired.current = false;
    if (!onLongPress) return;
    lp.current = setTimeout(() => { fired.current = true; onLongPress(); }, 460);
  };
  const cancelLP = () => { if (lp.current) clearTimeout(lp.current); };
  return (
    <button
      onPointerDown={startLP} onPointerUp={cancelLP} onPointerLeave={cancelLP} onPointerCancel={cancelLP}
      onContextMenu={(e) => e.preventDefault()}
      onClick={() => { if (fired.current) { fired.current = false; return; } onClick && onClick(); }}
      style={{
        flex: 1, minWidth: 0, height: '100%', display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', gap: 3,
        background: 'transparent', border: 'none', cursor: 'pointer', fontFamily: M_FONT, padding: '0 2px',
      }}>
      <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', gap: 3, animation: 'mPop .34s cubic-bezier(.2,.9,.3,1.2)' }}>
        <div style={{
          width: 56, height: 30, borderRadius: 16, display: 'flex', alignItems: 'center', justifyContent: 'center',
          background: active ? t.accentContainer : 'transparent', transition: 'background .2s',
          border: dim ? `1.5px dashed ${t.border}` : 'none',
        }}>
          <I size={21} color={active ? t.accent : (dim ? t.faint : t.sub)} />
        </div>
        <span style={{
          fontSize: 11.5, fontWeight: active ? 700 : 500, color: active ? t.accent : (dim ? t.faint : t.sub),
          maxWidth: 88, whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis',
        }}>{label}</span>
      </div>
    </button>
  );
}

// Вариант А: «Входящие» + до 3 закреплённых слотов (проекты ИЛИ умные списки) + FAB.
// Долгое нажатие по слоту — лист управления; пустой слот — тапом.
const M_SMART_VIEWS = {
  'sv:today': { iconName: 'TodayIcon', label: 'Сегодня', kind: 'today' },
  'sv:upcoming': { iconName: 'UpcomingIcon', label: 'Предстоящие', kind: 'upcoming' },
};

function MBottomNav({ t, view, searchOpen, pinned, onTab, onManagePins }) {
  const projects = window.M_PROJECTS || [];
  const slots = [0, 1, 2].map(i => {
    const id = pinned[i];
    if (!id) return null;
    if (M_SMART_VIEWS[id]) return { id, smart: M_SMART_VIEWS[id] };
    const p = projects.find(pp => pp.id === id);
    return p ? { id, project: p } : null;
  });
  return (
    <div style={{
      position: 'absolute', left: 0, right: 0, bottom: 0, height: 66, zIndex: 35,
      background: t.navBar, borderTop: `1px solid ${t.border}`,
      display: 'flex', alignItems: 'stretch', paddingBottom: 2,
      boxShadow: t.isDark ? 'none' : '0 -2px 16px rgba(0,0,0,0.04)',
    }}>
      <NavItem t={t} icon={InboxIcon} label="Входящие"
        active={!searchOpen && view.kind === 'inbox'} onClick={() => onTab('inbox')} />
      {slots.map((s, i) => s ? (
        s.smart ? (
          <NavItem key={s.id} t={t} icon={window[s.smart.iconName] || TodayIcon} label={s.smart.label}
            active={!searchOpen && view.kind === s.smart.kind}
            onClick={() => onTab(s.smart.kind)} onLongPress={onManagePins} />
        ) : (
          <NavItem key={s.id} t={t} icon={window[s.project.icon] || HashIcon} label={s.project.short || s.project.name}
            active={!searchOpen && view.kind === 'project' && view.projectId === s.project.id}
            onClick={() => onTab('project', s.project.id)} onLongPress={onManagePins} />
        )
      ) : (
        <NavItem key={'empty' + i} t={t} icon={PinIcon} label="Закрепить" dim
          onClick={onManagePins} onLongPress={onManagePins} />
      ))}
    </div>
  );
}

// ── Минимальный экран помощника ───────────────────────────────────────────
function MAIScreen({ t, onMenu }) {
  return (
    <div style={{ display: 'flex', flexDirection: 'column', height: '100%', background: t.bg }}>
      <MTopBar t={t} big onMenu={onMenu} title="Помощник" titleIcon={SparklesIcon} titleColor={t.accent} subtitle="Разбивка задач и быстрые подсказки" />
      <div style={{ flex: 1, overflow: 'auto', padding: '4px 16px 96px', display: 'flex', flexDirection: 'column', gap: 12 }}>
        {[
          { q: 'Разбей «Собрать Android-версию» на шаги', a: '1. Настроить сборку arm64\n2. Проверить тач-таргеты\n3. Прогнать на устройствах\n4. Снять видео' },
        ].map((m, i) => (
          <React.Fragment key={i}>
            <div style={{ alignSelf: 'flex-end', maxWidth: '82%', background: t.accent, color: t.accentFg, padding: '11px 15px', borderRadius: '18px 18px 4px 18px', fontSize: 14.5, lineHeight: 1.4 }}>{m.q}</div>
            <div style={{ alignSelf: 'flex-start', maxWidth: '88%', background: t.surface, color: t.text, padding: '13px 16px', borderRadius: '18px 18px 18px 4px', fontSize: 14.5, lineHeight: 1.5, whiteSpace: 'pre-line', border: `1px solid ${t.border}` }}>{m.a}</div>
          </React.Fragment>
        ))}
      </div>
      <div style={{ position: 'absolute', left: 0, right: 0, bottom: 0, padding: '10px 14px', background: t.bg, borderTop: `1px solid ${t.border}`, display: 'flex', gap: 10, alignItems: 'center' }}>
        <div style={{ flex: 1, height: 44, background: t.surface, borderRadius: 22, border: `1px solid ${t.border}`, display: 'flex', alignItems: 'center', padding: '0 16px', color: t.sub, fontSize: 14.5 }}>Спросить помощника…</div>
        <button style={{ width: 44, height: 44, borderRadius: 22, background: t.accent, border: 'none', display: 'flex', alignItems: 'center', justifyContent: 'center', cursor: 'pointer' }}><SendIcon size={18} color={t.accentFg} /></button>
      </div>
    </div>
  );
}

// ── Лист управления закреплёнными (умные списки + проекты) ────────────────
function MPinSheet({ open, onClose, t, pinned, onToggle }) {
  const projects = window.M_PROJECTS || [];
  const full = pinned.length >= 3;
  const smartItems = [
    { id: 'sv:today', name: 'Сегодня', Icon: TodayIcon, color: t.accent },
    { id: 'sv:upcoming', name: 'Предстоящие', Icon: UpcomingIcon, color: t.p3 },
  ];
  const row = (id, name, Icon, color) => {
    const on = pinned.includes(id);
    const idx = pinned.indexOf(id);
    return (
      <button key={id} onClick={() => onToggle(id)} style={{
        width: '100%', display: 'flex', alignItems: 'center', gap: 14, height: 54, padding: '0 14px',
        borderRadius: 14, border: 'none', cursor: 'pointer', fontFamily: M_FONT,
        background: on ? t.accentContainer : 'transparent',
      }}>
        <Icon size={22} color={color} />
        <span style={{ flex: 1, minWidth: 0, textAlign: 'left', fontSize: 15.5, fontWeight: on ? 700 : 500, color: t.text, whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>{name}</span>
        {on && <span style={{ fontSize: 12, fontWeight: 700, color: t.accent }}>№{idx + 1}</span>}
        <span style={{
          width: 30, height: 30, borderRadius: 15, display: 'flex', alignItems: 'center', justifyContent: 'center',
          background: on ? t.accent : 'transparent', border: on ? 'none' : `1.5px solid ${t.border}`,
        }}>
          <PinIcon size={16} color={on ? t.accentFg : t.faint} fill={on ? t.accentFg : 'none'} />
        </span>
      </button>
    );
  };
  const groupLabel = (txt) => (
    <div style={{ fontSize: 11.5, fontWeight: 700, color: t.sub, letterSpacing: 0.4, textTransform: 'uppercase', padding: '10px 16px 4px' }}>{txt}</div>
  );
  return (
    <MSheet open={open} onClose={onClose} t={t}>
      <div style={{ padding: '4px 20px 4px' }}>
        <div style={{ fontSize: 18, fontWeight: 800, color: t.text }}>Закреплённое в нижней панели</div>
        <div style={{ fontSize: 13, color: t.sub, marginTop: 4 }}>
          Слоты для умных списков и проектов · {pinned.length}/3{full ? ' · новый заменит самый старый' : ''}
        </div>
      </div>
      <div style={{ padding: '2px 10px 8px', maxHeight: 400, overflow: 'auto' }}>
        {groupLabel('Умные списки')}
        {smartItems.map(s => row(s.id, s.name, s.Icon, s.color))}
        {groupLabel('Проекты')}
        {projects.map(p => row(p.id, p.name, window[p.icon] || HashIcon, p.color))}
      </div>
      <div style={{ height: 14 }} />
    </MSheet>
  );
}

// ── Корневое приложение ───────────────────────────────────────────────────
function MobileApp({ themeName: themeProp = 'dark', density: densProp = 'spacious', navVariant: navProp = 'tabs4', initialView, initialListView, initialEditorTask, initialQuickAdd, initialSearch, initialDrawer, initialSelIds }) {
  const [themeName, setThemeName] = useStateA(themeProp);
  const [density, setDensity] = useStateA(densProp);
  const [navVariant, setNavVariant] = useStateA(navProp);
  useEffectA(() => setThemeName(themeProp), [themeProp]);
  useEffectA(() => setDensity(densProp), [densProp]);
  useEffectA(() => setNavVariant(navProp), [navProp]);

  const t0 = M_THEMES[themeName] || M_THEMES.dark;
  const [accent, setAccent] = useStateA(null);
  const [bigFont, setBigFont] = useStateA(false);
  const t = applyAccent(t0, accent);
  const d = M_DENSITY[density] || M_DENSITY.spacious;

  const [view, setView] = useStateA(initialView || { kind: 'inbox' });
  const [listView, setListView] = useStateA(initialListView || 'list');
  const [pinned, setPinned] = useStateA(['sv:today', 'opentodo', 'print3d']);
  const [tasks, setTasks] = useStateA(() => (window.M_TASKS || []).map(x => ({ ...x })));
  const [drawer, setDrawer] = useStateA(!!initialDrawer);
  const [editor, setEditor] = useStateA(initialEditorTask || null);
  const [quickAdd, setQuickAdd] = useStateA(!!initialQuickAdd);
  const [search, setSearch] = useStateA(!!initialSearch);
  const [pinSheet, setPinSheet] = useStateA(false);
  const [snack, setSnack] = useStateA(null);
  const [selMode, setSelMode] = useStateA(!!initialSelIds);
  const [selIds, setSelIds] = useStateA(initialSelIds || []);
  const [ctxTask, setCtxTask] = useStateA(null);
  const [movePicker, setMovePicker] = useStateA({ open: false, ids: [] });

  const tasksRef = useRefA(tasks); tasksRef.current = tasks;
  const snackTimer = useRefA(null);
  const showSnack = (msg, actionLabel, onAction) => {
    if (snackTimer.current) clearTimeout(snackTimer.current);
    setSnack({ msg, actionLabel, onAction: actionLabel ? () => { onAction && onAction(); setSnack(null); } : undefined });
    snackTimer.current = setTimeout(() => setSnack(null), 4200);
  };

  const toggle = (id) => {
    const tk = tasksRef.current.find(x => x.id === id);
    const willComplete = tk && !tk.done;
    const snap = tasksRef.current;
    setTasks(ts => ts.map(x => x.id === id ? { ...x, done: !x.done } : x));
    if (willComplete) showSnack('Задача выполнена', 'Отменить', () => setTasks(snap));
  };
  const remove = (id) => { const snap = tasksRef.current; setTasks(ts => ts.filter(x => x.id !== id)); showSnack('Задача удалена', 'Отменить', () => setTasks(snap)); };
  const snooze = (id) => { const snap = tasksRef.current; setTasks(ts => ts.map(x => x.id === id ? { ...x, dueKind: 'soon', due: 'Завтра', dueDay: 9 } : x)); showSnack('Отложено на завтра', 'Отменить', () => setTasks(snap)); };
  const addTask = (task) => { setTasks(ts => [task, ...ts]); showSnack('Задача добавлена в «Входящие»'); };

  const go = (v) => { setView(v); setSearch(false); setDrawer(false); setSelMode(false); setSelIds([]); if (v.kind !== 'project') setListView('list'); };
  const onTab = (kind, projectId) => {
    if (kind === 'search') { setSearch(true); return; }
    go(projectId ? { kind, projectId } : { kind });
  };
  const togglePin = (id) => setPinned(arr => arr.includes(id)
    ? arr.filter(x => x !== id)
    : (arr.length >= 3 ? [...arr.slice(1), id] : [...arr, id]));

  // Мультивыбор
  const enterSel = (id) => { setSelMode(true); setSelIds(id ? [id] : []); setCtxTask(null); };
  const toggleSel = (id) => setSelIds(s => s.includes(id) ? s.filter(x => x !== id) : [...s, id]);
  const exitSel = () => { setSelMode(false); setSelIds([]); };
  const bulkComplete = () => { const snap = tasksRef.current, ids = selIds; setTasks(ts => ts.map(x => ids.includes(x.id) ? { ...x, done: true } : x)); showSnack(`Выполнено: ${ids.length}`, 'Отменить', () => setTasks(snap)); exitSel(); };
  const bulkDelete = () => { const snap = tasksRef.current, ids = selIds; setTasks(ts => ts.filter(x => !ids.includes(x.id))); showSnack(`Удалено: ${ids.length}`, 'Отменить', () => setTasks(snap)); exitSel(); };
  const doMove = (projectId) => { const snap = tasksRef.current, ids = movePicker.ids; setTasks(ts => ts.map(x => ids.includes(x.id) ? { ...x, project: projectId } : x)); setMovePicker({ open: false, ids: [] }); const p = (window.M_PROJECTS || []).find(pp => pp.id === projectId); showSnack(`Перемещено → ${p ? p.name : ''}`, 'Отменить', () => setTasks(snap)); exitSel(); };

  let screen;
  if (view.kind === 'settings') {
    screen = <MSettingsScreen t={t} onMenu={() => setDrawer(true)} themeName={themeName} onTheme={setThemeName} density={density} onDensity={setDensity}
      accent={accent} onAccent={setAccent} bigFont={bigFont} onBigFont={setBigFont} />;
  } else if (view.kind === 'ai') {
    screen = <MAIScreen t={t} onMenu={() => setDrawer(true)} />;
  } else if (view.kind === 'notifications') {
    screen = <MNotificationsScreen t={t} onMenu={() => setDrawer(true)} onOpenTaskById={(id) => { const tk = tasksRef.current.find(x => x.id === id); if (tk) setEditor(tk); }} />;
  } else if (view.kind === 'productivity') {
    screen = <MProductivityScreen t={t} onMenu={() => setDrawer(true)} />;
  } else if (view.kind === 'trash') {
    screen = <MTrashScreen t={t} d={d} onMenu={() => setDrawer(true)} onSnack={(m) => showSnack(m)} />;
  } else if (view.kind === 'labels') {
    screen = <MLabelsScreen t={t} onMenu={() => setDrawer(true)} onOpenLabel={() => go({ kind: 'inbox' })} />;
  } else if (view.kind === 'filters') {
    screen = <MFiltersScreen t={t} onMenu={() => setDrawer(true)} />;
  } else if (view.kind === 'project' && listView === 'board') {
    screen = <MBoardScreen t={t} d={d} onMenu={() => setDrawer(true)} onSearch={() => setSearch(true)} onView={setListView} listView={listView} onOpenTask={setEditor} />;
  } else if (view.kind === 'project' && listView === 'cal') {
    screen = <MCalendarScreen t={t} d={d} view={view} tasks={tasks} onMenu={() => setDrawer(true)} onSearch={() => setSearch(true)} onView={setListView} onOpenTask={setEditor} />;
  } else {
    screen = <MListScreen t={t} d={d} view={view} tasks={tasks}
      onMenu={() => setDrawer(true)} onOpenTask={setEditor} onToggle={toggle} onSnooze={snooze} onDelete={remove}
      onSearch={() => setSearch(true)} onView={setListView} listView={listView} onQuickAdd={() => setQuickAdd(true)}
      selectionMode={selMode} selectedIds={selIds} onSelectToggle={toggleSel} onLongPress={(task) => setCtxTask(task)}
      onExitSel={exitSel} onBulkComplete={bulkComplete} onBulkDelete={bulkDelete} onBulkMove={() => setMovePicker({ open: true, ids: selIds })} />;
  }

  const noFabViews = ['settings', 'ai', 'notifications', 'productivity', 'trash', 'labels', 'filters'];
  const showFab = !selMode && !noFabViews.includes(view.kind);

  const ctxActions = ctxTask ? [
    { icon: NoteIcon, label: 'Открыть', onClick: () => { setEditor(ctxTask); setCtxTask(null); } },
    { icon: CheckCircleIcon, label: ctxTask.done ? 'Снять отметку' : 'Выполнить', color: t.success, onClick: () => { toggle(ctxTask.id); setCtxTask(null); } },
    { icon: ListIcon, label: 'Выделить несколько', onClick: () => enterSel(ctxTask.id) },
    { icon: FolderIcon, label: 'Переместить в проект', onClick: () => { setMovePicker({ open: true, ids: [ctxTask.id] }); setCtxTask(null); } },
    { icon: PlusIcon, label: 'Дублировать', onClick: () => { addTask({ ...ctxTask, id: 'dup' + Date.now() }); setCtxTask(null); } },
    { icon: TrashIcon, label: 'Удалить', color: t.error, onClick: () => { remove(ctxTask.id); setCtxTask(null); } },
  ] : [];

  return (
    <div style={{ position: 'relative', height: '100%', background: t.bg, fontFamily: M_FONT, overflow: 'hidden', zoom: bigFont ? 1.1 : 1 }}>
      {/* Контент со сдвигом под нижнюю навигацию */}
      <div style={{ position: 'absolute', inset: 0, bottom: 66 }}>
        {screen}
      </div>

      {showFab && <MFab t={t} bottom={66 + 18} onClick={() => setQuickAdd(true)} />}

      {!selMode && <MBottomNav t={t} view={view} searchOpen={search} pinned={pinned}
        onTab={onTab} onManagePins={() => setPinSheet(true)} />}

      {/* Снэкбар с отменой */}
      <MSnackbar snack={snack} t={t} bottom={selMode ? 18 : 78} />

      {/* Лист управления закреплёнными (долгое нажатие по слоту) */}
      <MPinSheet open={pinSheet} onClose={() => setPinSheet(false)} t={t} pinned={pinned} onToggle={togglePin} />

      {/* Контекст-меню задачи (долгое нажатие) */}
      <MContextSheet open={!!ctxTask} onClose={() => setCtxTask(null)} t={t} task={ctxTask} actions={ctxActions} />

      {/* Перемещение в проект */}
      <MOptionSheet open={movePicker.open} onClose={() => setMovePicker({ open: false, ids: [] })} t={t}
        title="Переместить в проект" subtitle={`${movePicker.ids.length} задач`}
        onSelect={doMove}
        options={(window.M_PROJECTS || []).map(p => ({ id: p.id, label: p.name, icon: window[p.icon] || HashIcon, color: p.color }))} />

      {/* Оверлеи */}
      <MDrawer open={drawer} onClose={() => setDrawer(false)} t={t} current={view} onNavigate={go}
        pinned={pinned} onTogglePin={togglePin} />
      {search && <MSearchScreen t={t} d={d} tasks={tasks} onClose={() => setSearch(false)} onOpenTask={(task) => { setSearch(false); setEditor(task); }} />}
      {editor && <MEditorScreen t={t} d={d} task={editor} onClose={() => setEditor(null)} />}
      <MQuickAdd open={quickAdd} onClose={() => setQuickAdd(false)} t={t} onAdd={addTask} />
    </div>
  );
}

Object.assign(window, { MobileApp, MBottomNav, NavItem, MAIScreen, MPinSheet });
