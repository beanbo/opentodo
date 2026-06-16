/*
 * Экраны-списки мобильного OpenTodo: список задач (Сегодня/Входящие/Проект/Предстоящие),
 * боковое меню (drawer) и доска (Kanban).
 */
const { useState: useStateS } = React;

// ── Верхняя панель экрана ─────────────────────────────────────────────────
function MTopBar({ t, onMenu, title, titleIcon: TI, titleColor, subtitle, right, big }) {
  return (
    <div style={{ background: t.bg, paddingTop: 2 }}>
      <div style={{ height: 56, display: 'flex', alignItems: 'center', padding: '0 6px 0 4px', gap: 2 }}>
        <button onClick={onMenu} style={iconBtnM(t)}>
          <MenuIcon size={23} color={t.text} />
        </button>
        {!big && (
          <div style={{ flex: 1, display: 'flex', alignItems: 'center', gap: 9, minWidth: 0 }}>
            {TI && <TI size={21} color={titleColor || t.accent} />}
            <span style={{ fontSize: 20, fontWeight: 700, color: t.text, whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>{title}</span>
          </div>
        )}
        {big && <div style={{ flex: 1 }} />}
        <div style={{ display: 'flex', alignItems: 'center' }}>{right}</div>
      </div>
      {big && (
        <div style={{ padding: '2px 20px 14px' }}>
          <div style={{ display: 'flex', alignItems: 'center', gap: 10 }}>
            {TI && <TI size={26} color={titleColor || t.accent} />}
            <span style={{ fontSize: 27, fontWeight: 800, color: t.text, letterSpacing: -0.3 }}>{title}</span>
          </div>
          {subtitle && <div style={{ fontSize: 13.5, color: t.sub, marginTop: 5, marginLeft: TI ? 36 : 0, fontWeight: 500 }}>{subtitle}</div>}
        </div>
      )}
    </div>
  );
}

function iconBtnM(t) {
  return {
    width: 46, height: 46, borderRadius: 23, border: 'none', background: 'transparent',
    display: 'flex', alignItems: 'center', justifyContent: 'center', cursor: 'pointer', flexShrink: 0,
  };
}

// Построение секций под выбранный экран
function buildSections(view, tasks) {
  const active = tasks.filter(x => !x.done);
  if (view.kind === 'today') {
    return [
      { id: 'over', title: 'Просрочено', accent: true, tasks: active.filter(x => x.dueKind === 'overdue') },
      { id: 'tod', title: 'Сегодня', tasks: active.filter(x => x.dueKind === 'today') },
    ].filter(s => s.tasks.length);
  }
  if (view.kind === 'upcoming') {
    const order = ['Завтра', '12 июня', '14 июня', '15 июня', '20 июня'];
    const groups = {};
    active.filter(x => x.dueKind === 'soon' || x.dueKind === 'future').forEach(x => {
      (groups[x.due] = groups[x.due] || []).push(x);
    });
    return Object.keys(groups).sort((a, b) => order.indexOf(a) - order.indexOf(b))
      .map(k => ({ id: k, title: k, tasks: groups[k] }));
  }
  if (view.kind === 'inbox') {
    return [{ id: 'inbox', title: 'Задачи', tasks: active.filter(x => x.project === 'opentodo') }];
  }
  // project
  const pt = active.filter(x => x.project === view.projectId);
  if (view.projectId === 'opentodo') {
    return [
      { id: 'work', title: 'В работе', tasks: pt.filter(x => x.dueKind === 'today' || x.dueKind === 'overdue') },
      { id: 'next', title: 'Дальше', tasks: pt.filter(x => x.dueKind === 'soon' || x.dueKind === 'future') },
    ].filter(s => s.tasks.length);
  }
  return [{ id: 'all', title: 'Задачи', tasks: pt }];
}

// ── Экран-список ──────────────────────────────────────────────────────────
function MListScreen({ t, d, view, tasks, onMenu, onOpenTask, onToggle, onSnooze, onDelete, onSearch, onView, listView, onQuickAdd,
  selectionMode, selectedIds, onSelectToggle, onLongPress, onExitSel, onBulkComplete, onBulkDelete, onBulkMove }) {
  const [collapsed, setCollapsed] = useStateS({});
  const [scrolled, setScrolled] = useStateS(false);
  const proj = (window.M_PROJECTS || []).find(p => p.id === view.projectId);

  const meta = {
    today: { title: 'Сегодня', icon: TodayIcon, color: t.accent, subtitle: '8 июня · понедельник' },
    inbox: { title: 'Входящие', icon: InboxIcon, color: t.accent, subtitle: null },
    upcoming: { title: 'Предстоящие', icon: UpcomingIcon, color: t.accent, subtitle: 'Июнь 2026' },
    project: { title: proj ? proj.name : 'Проект', icon: HashIcon, color: proj ? proj.color : t.accent, subtitle: proj ? `${proj.count} задач` : null },
  }[view.kind];

  const TitleIcon = view.kind === 'project' && proj && proj.icon === 'OpenTodoMark' ? OpenTodoMark : meta.icon;
  const sections = buildSections(view, tasks);
  const showProject = view.kind === 'today' || view.kind === 'upcoming' || view.kind === 'inbox';

  const emptyByView = {
    today: { icon: CheckCircleIcon, color: t.success, title: 'На сегодня всё', text: 'Задач на сегодня нет.\nОтличная работа!' },
    inbox: { icon: InboxIcon, color: t.accent, title: 'Входящие пусты', text: 'Новые задачи будут падать сюда.\nНажмите + чтобы добавить.' },
    upcoming: { icon: UpcomingIcon, color: t.accent, title: 'Ничего не запланировано', text: 'Поставьте задачам сроки —\nи они появятся здесь.' },
    project: { icon: TitleIcon, color: meta.color, title: 'Пока пусто', text: 'Добавьте первую задачу\nв этот проект.' },
  }[view.kind];

  const selCount = (selectedIds || []).length;

  return (
    <div style={{ display: 'flex', flexDirection: 'column', height: '100%', background: t.bg }}>
      {selectionMode ? (
        <div style={{ background: t.accentContainer, paddingTop: 2 }}>
          <div style={{ height: 56, display: 'flex', alignItems: 'center', padding: '0 6px', gap: 2 }}>
            <button aria-label="Выйти из выбора" onClick={onExitSel} style={iconBtnM(t)}>
              <span style={{ fontSize: 26, color: t.accent, lineHeight: 1 }}>×</span>
            </button>
            <span style={{ flex: 1, fontSize: 18, fontWeight: 800, color: t.accent, paddingLeft: 6 }}>{selCount} выбрано</span>
            <button aria-label="Выполнить выбранные" onClick={onBulkComplete} style={iconBtnM(t)}><CheckCircleIcon size={22} color={t.accent} /></button>
            <button aria-label="Переместить выбранные" onClick={onBulkMove} style={iconBtnM(t)}><FolderIcon size={21} color={t.accent} /></button>
            <button aria-label="Удалить выбранные" onClick={onBulkDelete} style={iconBtnM(t)}><TrashIcon size={21} color={t.error} /></button>
          </div>
        </div>
      ) : (
        <MTopBar t={t} big={!scrolled} onMenu={onMenu}
          title={meta.title} titleIcon={TitleIcon} titleColor={meta.color} subtitle={meta.subtitle}
          right={(
            <React.Fragment>
              <button aria-label="Поиск" onClick={onSearch} style={iconBtnM(t)}><SearchIcon size={21} color={t.text} /></button>
              <button aria-label="Ещё" style={iconBtnM(t)}><MoreIcon size={21} color={t.text} /></button>
            </React.Fragment>
          )}
        />
      )}

      {/* Переключатель вида — для проекта */}
      {view.kind === 'project' && !selectionMode && (
        <div style={{ padding: '0 16px 12px' }}>
          <MSeg t={t} value={listView || 'list'} onChange={onView}
            options={[
              { value: 'list', label: 'Список', icon: ListIcon },
              { value: 'board', label: 'Доска', icon: BoardIcon },
              { value: 'cal', label: 'Календарь', icon: CalendarIcon },
            ]} />
        </div>
      )}

      {/* Список */}
      <div style={{ flex: 1, overflow: 'auto', paddingBottom: 96 }}
        onScroll={(e) => { const s = e.target.scrollTop > 24; if (s !== scrolled) setScrolled(s); }}>
        {sections.length === 0 && (
          <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', padding: '72px 32px', textAlign: 'center', color: t.sub }}>
            <div style={{ width: 80, height: 80, borderRadius: 40, background: t.surface, display: 'flex', alignItems: 'center', justifyContent: 'center', marginBottom: 18, border: `1px solid ${t.border}` }}>
              <emptyByView.icon size={38} color={emptyByView.color} />
            </div>
            <div style={{ fontSize: 17.5, fontWeight: 700, color: t.text }}>{emptyByView.title}</div>
            <div style={{ fontSize: 14, marginTop: 7, lineHeight: 1.5, whiteSpace: 'pre-line' }}>{emptyByView.text}</div>
          </div>
        )}
        {sections.map(s => {
          const isCol = collapsed[s.id];
          return (
            <div key={s.id}>
              <MSection t={t} d={d} title={s.title} count={s.tasks.length}
                collapsed={isCol} onToggle={() => setCollapsed(c => ({ ...c, [s.id]: !c[s.id] }))} />
              {!isCol && s.tasks.map(task => (
                <MTaskRow key={task.id} task={task} t={t} d={d} showProject={showProject}
                  selectionMode={selectionMode} selected={(selectedIds || []).includes(task.id)}
                  onSelectToggle={onSelectToggle} onLongPress={onLongPress}
                  onToggle={() => onToggle(task.id)} onOpen={() => onOpenTask(task)}
                  onSnooze={() => onSnooze(task.id)} onDelete={() => onDelete(task.id)} />
              ))}
            </div>
          );
        })}
      </div>
    </div>
  );
}

// ── Боковое меню (drawer) ─────────────────────────────────────────────────
function MDrawerItem({ icon: I, iconColor, label, count, active, t, onClick, badge, badgeColor, pinned, onPin }) {
  return (
    <div style={{
      width: '100%', display: 'flex', alignItems: 'center', gap: 15,
      height: 50, padding: '0 8px 0 14px', borderRadius: 13,
      background: active ? t.accentContainer : 'transparent', fontFamily: M_FONT,
    }}>
      <button onClick={onClick} style={{ flex: 1, minWidth: 0, display: 'flex', alignItems: 'center', gap: 15, height: '100%', background: 'transparent', border: 'none', cursor: 'pointer', padding: 0 }}>
        <I size={21} color={active ? t.accent : iconColor || t.sub} />
        <span style={{ flex: 1, minWidth: 0, textAlign: 'left', fontSize: 15.5, fontWeight: active ? 700 : 500, color: active ? t.accent : t.text, whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>{label}</span>
      </button>
      {badge != null && (
        <span style={{ background: badgeColor || t.error, color: '#fff', fontSize: 11, fontWeight: 700, borderRadius: 9, padding: '2px 7px', minWidth: 18, textAlign: 'center' }}>{badge}</span>
      )}
      {count != null && <span style={{ fontSize: 13, color: t.sub, fontWeight: 600 }}>{count}</span>}
      {onPin && (
        <button onClick={(e) => { e.stopPropagation(); onPin(); }} style={{
          width: 36, height: 36, borderRadius: 18, border: 'none', cursor: 'pointer', flexShrink: 0,
          background: pinned ? t.accentContainer : 'transparent',
          display: 'flex', alignItems: 'center', justifyContent: 'center',
          transform: pinned ? 'none' : 'rotate(0deg)',
        }} title={pinned ? 'Открепить' : 'Закрепить в навигации'}>
          <PinIcon size={17} color={pinned ? t.accent : t.faint} fill={pinned ? t.accent : 'none'} />
        </button>
      )}
    </div>
  );
}

function MDrawer({ open, onClose, t, current, onNavigate, pinned = [], onTogglePin }) {
  return (
    <React.Fragment>
      <div onClick={onClose} style={{
        position: 'absolute', inset: 0, background: t.scrim, zIndex: 50,
        opacity: open ? 1 : 0, pointerEvents: open ? 'auto' : 'none', transition: 'opacity .25s',
      }} />
      <div style={{
        position: 'absolute', top: 0, bottom: 0, left: 0, width: 308, zIndex: 51,
        background: t.surface, transform: open ? 'translateX(0)' : 'translateX(-100%)',
        transition: 'transform .3s cubic-bezier(.2,.8,.2,1)', display: 'flex', flexDirection: 'column',
        boxShadow: open ? '4px 0 40px rgba(0,0,0,0.35)' : 'none',
      }}>
        {/* Профиль */}
        <div style={{ padding: '46px 18px 14px', display: 'flex', alignItems: 'center', gap: 13 }}>
          <div style={{ width: 46, height: 46, borderRadius: 23, background: t.accent, display: 'flex', alignItems: 'center', justifyContent: 'center', color: '#fff', fontWeight: 700, fontSize: 18 }}>D</div>
          <div style={{ flex: 1, minWidth: 0 }}>
            <div style={{ fontSize: 15.5, fontWeight: 700, color: t.text }}>Денис</div>
            <div style={{ fontSize: 12.5, color: t.sub, display: 'flex', alignItems: 'center', gap: 5 }}>
              <OfflineDotIcon size={9} color={t.sub} /> Офлайн · локально
            </div>
          </div>
          <SyncIcon size={20} color={t.sub} />
        </div>

        <div style={{ flex: 1, overflow: 'auto', padding: '6px 12px 12px' }}>
          <MDrawerItem t={t} icon={InboxIcon} label="Входящие" count={14} active={current.kind === 'inbox'} onClick={() => onNavigate({ kind: 'inbox' })} />
          <MDrawerItem t={t} icon={TodayIcon} label="Сегодня" badge={3} active={current.kind === 'today'} onClick={() => onNavigate({ kind: 'today' })} />
          <MDrawerItem t={t} icon={UpcomingIcon} label="Предстоящие" active={current.kind === 'upcoming'} onClick={() => onNavigate({ kind: 'upcoming' })} />
          <MDrawerItem t={t} icon={BellIcon} label="Уведомления" badge={2} active={current.kind === 'notifications'} onClick={() => onNavigate({ kind: 'notifications' })} />
          <MDrawerItem t={t} icon={ProductivityIcon} label="Продуктивность" active={current.kind === 'productivity'} onClick={() => onNavigate({ kind: 'productivity' })} />
          <MDrawerItem t={t} icon={SparklesIcon} label="Помощник" active={current.kind === 'ai'} onClick={() => onNavigate({ kind: 'ai' })} />

          <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', padding: '18px 12px 4px' }}>
            <span style={{ fontSize: 12, fontWeight: 700, color: t.sub, letterSpacing: 0.4, textTransform: 'uppercase' }}>Проекты</span>
            <PlusIcon size={16} color={t.sub} />
          </div>
          <div style={{ fontSize: 11.5, color: t.faint, padding: '0 14px 8px', lineHeight: 1.4 }}>
            Нажмите на булавку, чтобы закрепить проект в нижней панели ({pinned.length}/3)
          </div>
          {(window.M_PROJECTS || []).map(p => {
            const I = window[p.icon] || HashIcon;
            return (
              <MDrawerItem key={p.id} t={t} icon={I} iconColor={p.color} label={p.name}
                active={current.kind === 'project' && current.projectId === p.id}
                onClick={() => onNavigate({ kind: 'project', projectId: p.id })}
                pinned={pinned.includes(p.id)} onPin={() => onTogglePin && onTogglePin(p.id)} />
            );
          })}

          <div style={{ height: 1, background: t.border, margin: '12px 8px' }} />
          <MDrawerItem t={t} icon={TagIcon} label="Метки" active={current.kind === 'labels'} onClick={() => onNavigate({ kind: 'labels' })} />
          <MDrawerItem t={t} icon={FilterIcon} label="Фильтры" active={current.kind === 'filters'} onClick={() => onNavigate({ kind: 'filters' })} />
          <MDrawerItem t={t} icon={TrashIcon} label="Корзина" active={current.kind === 'trash'} onClick={() => onNavigate({ kind: 'trash' })} />
        </div>

        <button onClick={() => onNavigate({ kind: 'settings' })} style={{
          display: 'flex', alignItems: 'center', gap: 15, height: 56, padding: '0 26px',
          borderTop: `1px solid ${t.border}`, background: 'transparent', border: 'none',
          borderTopWidth: 1, borderTopStyle: 'solid', borderTopColor: t.border, cursor: 'pointer', fontFamily: M_FONT,
        }}>
          <SettingsIcon size={21} color={t.sub} />
          <span style={{ fontSize: 15, fontWeight: 600, color: t.text }}>Настройки</span>
        </button>
      </div>
    </React.Fragment>
  );
}

// ── Доска (Kanban) ────────────────────────────────────────────────────────
function MBoardScreen({ t, d, onMenu, onSearch, onView, listView, onOpenTask }) {
  return (
    <div style={{ display: 'flex', flexDirection: 'column', height: '100%', background: t.bg }}>
      <MTopBar t={t} onMenu={onMenu} title="OpenTodo" titleIcon={OpenTodoMark} titleColor={t.accent}
        right={(
          <React.Fragment>
            <button onClick={onSearch} style={iconBtnM(t)}><SearchIcon size={21} color={t.text} /></button>
            <button style={iconBtnM(t)}><MoreIcon size={21} color={t.text} /></button>
          </React.Fragment>
        )} />
      <div style={{ padding: '0 16px 12px' }}>
        <MSeg t={t} value="board" onChange={onView}
          options={[
            { value: 'list', label: 'Список', icon: ListIcon },
            { value: 'board', label: 'Доска', icon: BoardIcon },
            { value: 'cal', label: 'Календарь', icon: CalendarIcon },
          ]} />
      </div>
      <div style={{ flex: 1, overflow: 'auto', display: 'flex', gap: 14, padding: '4px 16px 96px', scrollSnapType: 'x mandatory' }}>
        {(window.M_BOARD || []).map(col => (
          <div key={col.id} style={{ width: 280, flexShrink: 0, scrollSnapAlign: 'start', display: 'flex', flexDirection: 'column' }}>
            <div style={{ display: 'flex', alignItems: 'center', gap: 8, padding: '6px 4px 12px' }}>
              <span style={{ width: 9, height: 9, borderRadius: 5, background: col.accent ? t.accent : t.faint }} />
              <span style={{ fontSize: 15, fontWeight: 700, color: t.text }}>{col.title}</span>
              <span style={{ fontSize: 13, color: t.sub, fontWeight: 600 }}>{col.cards.length}</span>
              <div style={{ flex: 1 }} />
              <PlusIcon size={17} color={t.sub} />
            </div>
            <div style={{ display: 'flex', flexDirection: 'column', gap: 11 }}>
              {col.cards.map(c => {
                const pc = M_PRIORITY_COLOR(t, c.priority);
                return (
                  <button key={c.id} onClick={() => onOpenTask({ ...c, project: 'opentodo' })} style={{
                    textAlign: 'left', background: t.card, border: `1px solid ${t.border}`, borderRadius: 14,
                    padding: '13px 14px', cursor: 'pointer', fontFamily: M_FONT, display: 'flex', flexDirection: 'column', gap: 9,
                    boxShadow: t.isDark ? 'none' : '0 1px 2px rgba(0,0,0,0.04)',
                  }}>
                    <div style={{ display: 'flex', alignItems: 'flex-start', gap: 10 }}>
                      <span style={{ width: 18, height: 18, borderRadius: 9, border: `2px solid ${c.done ? t.success : pc}`, background: c.done ? t.success : 'transparent', flexShrink: 0, marginTop: 1, display: 'flex', alignItems: 'center', justifyContent: 'center' }}>
                        {c.done && <CheckIcon size={10} color="#fff" stroke={2.4} />}
                      </span>
                      <span style={{ fontSize: 14.5, color: c.done ? t.sub : t.text, lineHeight: 1.35, textDecoration: c.done ? 'line-through' : 'none' }}>{c.text}</span>
                    </div>
                    {(c.due || c.labels || c.sub || c.recurring) && (
                      <div style={{ display: 'flex', gap: 8, flexWrap: 'wrap', paddingLeft: 28 }}>
                        {c.recurring && <MChip icon={RepeatIcon} color={t.accent} t={t} d={d}>повтор</MChip>}
                        {c.due && <MChip icon={CalendarIcon} color={c.dueKind === 'overdue' ? t.error : c.dueKind === 'today' ? t.success : t.sub} t={t} d={d}>{c.due}</MChip>}
                        {c.sub && <MChip icon={ListIcon} color={t.sub} t={t} d={d}>{c.sub.done}/{c.sub.total}</MChip>}
                        {c.labels && c.labels.map(l => {
                          const lab = (window.M_LABELS || []).find(x => x.name === l);
                          return <MChip key={l} icon={TagIcon} color={lab ? lab.color : t.sub} t={t} d={d} tinted>{l}</MChip>;
                        })}
                      </div>
                    )}
                  </button>
                );
              })}
              <button style={{ display: 'flex', alignItems: 'center', gap: 8, padding: '11px 12px', borderRadius: 12, border: `1.5px dashed ${t.border}`, background: 'transparent', color: t.sub, cursor: 'pointer', fontFamily: M_FONT, fontSize: 13.5, fontWeight: 600 }}>
                <PlusIcon size={15} color={t.sub} /> Добавить
              </button>
            </div>
          </div>
        ))}
      </div>
    </div>
  );
}

Object.assign(window, { MTopBar, MListScreen, MDrawer, MDrawerItem, MBoardScreen, iconBtnM, buildSections });
