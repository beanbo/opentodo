/*
 * Дополнительные экраны: Календарь-месяц, Уведомления, Продуктивность,
 * Корзина, Метки, Фильтры.
 */
const { useState: useStateT } = React;

// ── Календарь-месяц ───────────────────────────────────────────────────────
function MCalendarScreen({ t, d, view, tasks, onMenu, onSearch, onView, onOpenTask }) {
  const proj = (window.M_PROJECTS || []).find(p => p.id === view.projectId);
  const [day, setDay] = useStateT(8);
  const [ym, setYm] = useStateT({ y: 2026, m: 5 }); // июнь 2026
  const isJune = ym.y === 2026 && ym.m === 5;
  const marks = {};
  if (isJune) tasks.filter(x => !x.done && x.dueDay).forEach(x => {
    (marks[x.dueDay] = marks[x.dueDay] || []).push(M_PRIORITY_COLOR(t, x.priority));
  });
  const dayTasks = isJune ? tasks.filter(x => !x.done && x.dueDay === day) : [];
  const shift = (dir) => setYm(({ y, m }) => {
    const nm = m + dir;
    return nm < 0 ? { y: y - 1, m: 11 } : nm > 11 ? { y: y + 1, m: 0 } : { y, m: nm };
  });

  return (
    <div style={{ display: 'flex', flexDirection: 'column', height: '100%', background: t.bg }}>
      <MTopBar t={t} onMenu={onMenu} title={proj ? proj.name : 'Календарь'} titleIcon={proj ? (window[proj.icon] || HashIcon) : CalendarIcon} titleColor={proj ? proj.color : t.accent}
        right={(
          <React.Fragment>
            <button aria-label="Поиск" onClick={onSearch} style={iconBtnM(t)}><SearchIcon size={21} color={t.text} /></button>
            <button aria-label="Ещё" style={iconBtnM(t)}><MoreIcon size={21} color={t.text} /></button>
          </React.Fragment>
        )} />
      {view.kind === 'project' && (
        <div style={{ padding: '0 16px 12px' }}>
          <MSeg t={t} value="cal" onChange={onView}
            options={[{ value: 'list', label: 'Список', icon: ListIcon }, { value: 'board', label: 'Доска', icon: BoardIcon }, { value: 'cal', label: 'Календарь', icon: CalendarIcon }]} />
        </div>
      )}
      <div style={{ flex: 1, overflow: 'auto', paddingBottom: 96 }}>
        <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', padding: '4px 20px 10px' }}>
          <div style={{ fontSize: 18, fontWeight: 800, color: t.text }}>{M_MONTHS[ym.m]} {ym.y}</div>
          <div style={{ display: 'flex', gap: 4 }}>
            <button aria-label="Предыдущий месяц" onClick={() => shift(-1)} style={iconBtnM(t)}><ChevronRightIcon size={20} color={t.sub} style={{ transform: 'rotate(180deg)' }} /></button>
            <button aria-label="Следующий месяц" onClick={() => shift(1)} style={iconBtnM(t)}><ChevronRightIcon size={20} color={t.sub} /></button>
          </div>
        </div>
        <div style={{ padding: '0 8px' }}>
          <MCalendar t={t} year={ym.y} month={ym.m} marks={marks} selectedDay={isJune ? day : null} today={isJune ? 8 : null} onPick={(dd) => { if (isJune) setDay(dd); }} />
        </div>
        <div style={{ padding: '14px 20px 8px', fontSize: 13, fontWeight: 700, color: t.sub, letterSpacing: 0.3, textTransform: 'uppercase' }}>
          {isJune ? `${day} июня · ${dayTasks.length === 0 ? 'нет задач' : dayTasks.length + ' задач'}` : 'Нет задач в этом месяце'}
        </div>
        {dayTasks.map(task => (
          <MTaskRow key={task.id} task={task} t={t} d={d} showProject onToggle={() => {}} onOpen={() => onOpenTask(task)} onSnooze={() => {}} onDelete={() => {}} />
        ))}
        {dayTasks.length === 0 && (
          <div style={{ textAlign: 'center', color: t.sub, padding: '30px 24px', fontSize: 14 }}>Свободный день — ничего не запланировано</div>
        )}
      </div>
    </div>
  );
}

// ── Уведомления ───────────────────────────────────────────────────────────
function MNotificationsScreen({ t, onMenu, onOpenTaskById }) {
  const [items, setItems] = useStateT(window.M_NOTIFICATIONS || []);
  return (
    <div style={{ display: 'flex', flexDirection: 'column', height: '100%', background: t.bg }}>
      <MTopBar t={t} big onMenu={onMenu} title="Уведомления" titleIcon={BellIcon} titleColor={t.accent}
        right={<button aria-label="Прочитать все" onClick={() => setItems(its => its.map(x => ({ ...x, unread: false })))} style={iconBtnM(t)}><CheckCircleIcon size={21} color={t.text} /></button>} />
      <div style={{ flex: 1, overflow: 'auto', paddingBottom: 96 }}>
        {items.map(n => {
          const Ic = window[n.icon] || BellIcon;
          return (
            <button key={n.id} onClick={() => n.taskId && onOpenTaskById(n.taskId)} style={{
              width: '100%', textAlign: 'left', display: 'flex', gap: 14, padding: '15px 18px', cursor: 'pointer', fontFamily: M_FONT,
              background: n.unread ? (t.isDark ? 'rgba(255,255,255,0.03)' : 'rgba(0,0,0,0.02)') : 'transparent',
              border: 'none', borderBottom: `1px solid ${t.hairline}`,
            }}>
              <span style={{ width: 40, height: 40, borderRadius: 12, background: `${n.color}1f`, display: 'flex', alignItems: 'center', justifyContent: 'center', flexShrink: 0 }}>
                <Ic size={20} color={n.color} />
              </span>
              <div style={{ flex: 1, minWidth: 0 }}>
                <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
                  <span style={{ fontSize: 15, fontWeight: 700, color: t.text, flex: 1 }}>{n.title}</span>
                  {n.unread && <span style={{ width: 8, height: 8, borderRadius: 4, background: t.accent, flexShrink: 0 }} />}
                </div>
                <div style={{ fontSize: 13.5, color: t.sub, marginTop: 3, lineHeight: 1.4 }}>{n.body}</div>
                <div style={{ fontSize: 12, color: t.faint, marginTop: 5 }}>{n.when}</div>
              </div>
            </button>
          );
        })}
      </div>
    </div>
  );
}

// ── Продуктивность ────────────────────────────────────────────────────────
function MProductivityScreen({ t, onMenu }) {
  const P = window.M_PRODUCTIVITY;
  const ring = Math.round(P.todayDone / P.todayTotal * 100);
  const maxBar = Math.max(...P.week.map(w => w.done), 6);
  return (
    <div style={{ display: 'flex', flexDirection: 'column', height: '100%', background: t.bg }}>
      <MTopBar t={t} big onMenu={onMenu} title="Продуктивность" titleIcon={ProductivityIcon} titleColor={t.accent} subtitle={`Уровень: ${P.level} · ${P.karma.toLocaleString('ru')} кармы`} />
      <div style={{ flex: 1, overflow: 'auto', padding: '4px 14px 96px' }}>
        {/* Верхние карточки */}
        <div style={{ display: 'flex', gap: 12, marginBottom: 14 }}>
          <div style={{ flex: 1, background: t.surface, border: `1px solid ${t.border}`, borderRadius: 16, padding: '16px 16px' }}>
            <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
              <FlameIcon size={22} color="#eb8909" />
              <span style={{ fontSize: 30, fontWeight: 800, color: t.text }}>{P.streak}</span>
            </div>
            <div style={{ fontSize: 13, color: t.sub, marginTop: 6 }}>дней подряд</div>
          </div>
          <div style={{ flex: 1, background: t.surface, border: `1px solid ${t.border}`, borderRadius: 16, padding: '14px 16px', display: 'flex', alignItems: 'center', gap: 14 }}>
            <div style={{ width: 56, height: 56, borderRadius: 28, flexShrink: 0, background: `conic-gradient(${t.accent} ${ring * 3.6}deg, ${t.hairline} 0)`, display: 'flex', alignItems: 'center', justifyContent: 'center' }}>
              <div style={{ width: 42, height: 42, borderRadius: 21, background: t.surface, display: 'flex', alignItems: 'center', justifyContent: 'center', fontSize: 13, fontWeight: 800, color: t.text }}>{ring}%</div>
            </div>
            <div>
              <div style={{ fontSize: 20, fontWeight: 800, color: t.text }}>{P.todayDone}/{P.todayTotal}</div>
              <div style={{ fontSize: 12.5, color: t.sub, marginTop: 2 }}>сегодня</div>
            </div>
          </div>
        </div>

        {/* Недельный график */}
        <div style={{ background: t.surface, border: `1px solid ${t.border}`, borderRadius: 16, padding: '16px 16px 12px', marginBottom: 14 }}>
          <div style={{ display: 'flex', alignItems: 'baseline', justifyContent: 'space-between', marginBottom: 14 }}>
            <span style={{ fontSize: 15, fontWeight: 700, color: t.text }}>Эта неделя</span>
            <span style={{ fontSize: 13, color: t.sub }}>{P.weekDone} задач</span>
          </div>
          <div style={{ display: 'flex', alignItems: 'flex-end', gap: 8, height: 120 }}>
            {P.week.map(w => (
              <div key={w.d} style={{ flex: 1, display: 'flex', flexDirection: 'column', alignItems: 'center', gap: 7 }}>
                <div style={{ width: '100%', flex: 1, display: 'flex', alignItems: 'flex-end' }}>
                  <div style={{ width: '100%', height: `${w.done / maxBar * 100}%`, minHeight: 6, borderRadius: 7, background: w.done >= w.goal ? t.accent : `${t.accent}66` }} />
                </div>
                <span style={{ fontSize: 11.5, color: t.sub, fontWeight: 600 }}>{w.d}</span>
              </div>
            ))}
          </div>
        </div>

        {/* Карма */}
        <div style={{ background: t.surface, border: `1px solid ${t.border}`, borderRadius: 16, padding: '16px 18px', display: 'flex', alignItems: 'center', gap: 14 }}>
          <span style={{ width: 44, height: 44, borderRadius: 22, background: `${t.accent}1f`, display: 'flex', alignItems: 'center', justifyContent: 'center', flexShrink: 0 }}>
            <StarIcon size={22} color={t.accent} />
          </span>
          <div style={{ flex: 1 }}>
            <div style={{ fontSize: 15, fontWeight: 700, color: t.text }}>{P.karma.toLocaleString('ru')} кармы · {P.level}</div>
            <div style={{ height: 6, background: t.hairline, borderRadius: 3, marginTop: 8, overflow: 'hidden' }}>
              <div style={{ width: '64%', height: '100%', background: t.accent, borderRadius: 3 }} />
            </div>
            <div style={{ fontSize: 12, color: t.sub, marginTop: 6 }}>До «Мастера» ещё 760</div>
          </div>
        </div>
      </div>
    </div>
  );
}

// ── Корзина ───────────────────────────────────────────────────────────────
function MTrashScreen({ t, d, onMenu, onSnack }) {
  const [items, setItems] = useStateT(window.M_TRASH || []);
  const restore = (id) => { setItems(its => its.filter(x => x.id !== id)); onSnack && onSnack('Задача восстановлена'); };
  return (
    <div style={{ display: 'flex', flexDirection: 'column', height: '100%', background: t.bg }}>
      <MTopBar t={t} big onMenu={onMenu} title="Корзина" titleIcon={TrashIcon} titleColor={t.sub}
        right={<button aria-label="Очистить корзину" onClick={() => { setItems([]); onSnack && onSnack('Корзина очищена'); }} style={iconBtnM(t)}><TrashIcon size={20} color={t.error} /></button>} />
      <div style={{ flex: 1, overflow: 'auto', paddingBottom: 96 }}>
        <div style={{ fontSize: 12.5, color: t.sub, padding: '4px 20px 10px', lineHeight: 1.4 }}>Удалённые задачи хранятся 30 дней, затем стираются автоматически.</div>
        {items.length === 0 && (
          <div style={{ textAlign: 'center', color: t.sub, padding: '60px 24px' }}>
            <TrashIcon size={40} color={t.faint} />
            <div style={{ fontSize: 15, marginTop: 12 }}>Корзина пуста</div>
          </div>
        )}
        {items.map(it => {
          const proj = (window.M_PROJECTS || []).find(p => p.id === it.project);
          return (
            <div key={it.id} style={{ display: 'flex', alignItems: 'center', gap: 12, padding: '13px 18px', borderBottom: `1px solid ${t.hairline}` }}>
              <span style={{ width: 18, height: 18, borderRadius: 9, border: `2px solid ${t.faint}`, flexShrink: 0 }} />
              <div style={{ flex: 1, minWidth: 0 }}>
                <div style={{ fontSize: 14.5, color: t.sub, textDecoration: 'line-through', whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>{it.text}</div>
                <div style={{ fontSize: 12, color: t.faint, marginTop: 3 }}>{proj ? proj.name + ' · ' : ''}удалено {it.deletedAt}</div>
              </div>
              <button aria-label="Восстановить" onClick={() => restore(it.id)} style={{ width: 40, height: 40, borderRadius: 20, border: 'none', background: t.accentContainer, display: 'flex', alignItems: 'center', justifyContent: 'center', cursor: 'pointer', flexShrink: 0 }}>
                <ResetIcon size={18} color={t.accent} />
              </button>
            </div>
          );
        })}
      </div>
    </div>
  );
}

// ── Метки ─────────────────────────────────────────────────────────────────
function MLabelsScreen({ t, onMenu, onOpenLabel }) {
  const labels = window.M_LABELS || [];
  const counts = { 'баг': 4, 'релиз': 2, 'идея': 3 };
  return (
    <div style={{ display: 'flex', flexDirection: 'column', height: '100%', background: t.bg }}>
      <MTopBar t={t} big onMenu={onMenu} title="Метки" titleIcon={TagIcon} titleColor={t.accent}
        right={<button aria-label="Добавить метку" style={iconBtnM(t)}><PlusIcon size={22} color={t.text} /></button>} />
      <div style={{ flex: 1, overflow: 'auto', paddingBottom: 96 }}>
        {labels.map(l => (
          <button key={l.name} onClick={() => onOpenLabel && onOpenLabel(l.name)} style={{
            width: '100%', display: 'flex', alignItems: 'center', gap: 14, height: 60, padding: '0 18px',
            background: 'transparent', border: 'none', borderBottom: `1px solid ${t.hairline}`, cursor: 'pointer', fontFamily: M_FONT,
          }}>
            <span style={{ width: 38, height: 38, borderRadius: 19, background: `${l.color}1f`, display: 'flex', alignItems: 'center', justifyContent: 'center', flexShrink: 0 }}>
              <TagIcon size={19} color={l.color} />
            </span>
            <span style={{ flex: 1, textAlign: 'left', fontSize: 15.5, fontWeight: 600, color: t.text }}>{l.name}</span>
            <span style={{ fontSize: 13.5, color: t.sub, fontWeight: 600 }}>{counts[l.name] || 0}</span>
            <ChevronRightIcon size={17} color={t.faint} />
          </button>
        ))}
      </div>
    </div>
  );
}

// ── Фильтры ───────────────────────────────────────────────────────────────
function MFiltersScreen({ t, onMenu }) {
  const filters = window.M_FILTERS || [];
  return (
    <div style={{ display: 'flex', flexDirection: 'column', height: '100%', background: t.bg }}>
      <MTopBar t={t} big onMenu={onMenu} title="Фильтры" titleIcon={FilterIcon} titleColor={t.accent}
        right={<button aria-label="Создать фильтр" style={iconBtnM(t)}><PlusIcon size={22} color={t.text} /></button>} />
      <div style={{ flex: 1, overflow: 'auto', paddingBottom: 96 }}>
        {filters.map(f => {
          const I = window[f.icon] || FilterIcon;
          return (
            <button key={f.id} style={{
              width: '100%', display: 'flex', alignItems: 'center', gap: 14, height: 64, padding: '0 18px',
              background: 'transparent', border: 'none', borderBottom: `1px solid ${t.hairline}`, cursor: 'pointer', fontFamily: M_FONT,
            }}>
              <span style={{ width: 38, height: 38, borderRadius: 19, background: `${f.color}1f`, display: 'flex', alignItems: 'center', justifyContent: 'center', flexShrink: 0 }}>
                <I size={19} color={f.color} />
              </span>
              <div style={{ flex: 1, minWidth: 0, textAlign: 'left' }}>
                <div style={{ fontSize: 15.5, fontWeight: 600, color: t.text }}>{f.name}</div>
                <div style={{ fontSize: 12.5, color: t.faint, marginTop: 2, fontFamily: 'JetBrains Mono, monospace' }}>{f.query}</div>
              </div>
              <span style={{ fontSize: 13.5, color: t.sub, fontWeight: 600 }}>{f.count}</span>
              <ChevronRightIcon size={17} color={t.faint} />
            </button>
          );
        })}
      </div>
    </div>
  );
}

Object.assign(window, { MCalendarScreen, MNotificationsScreen, MProductivityScreen, MTrashScreen, MLabelsScreen, MFiltersScreen });
