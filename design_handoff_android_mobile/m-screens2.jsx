/*
 * Детальные экраны: редактор задачи, быстрое добавление, поиск, настройки, помощник.
 * Плюс лёгкая тематическая клавиатура (стартовая — только светлая).
 */
const { useState: useStateD } = React;

// ── Тематическая клавиатура ───────────────────────────────────────────────
function MKeyboard({ t, accentKey }) {
  const kbBg = t.isDark ? '#1b1b1b' : '#d6d8dd';
  const keyBg = t.isDark ? '#3a3a3c' : '#ffffff';
  const keyFg = t.isDark ? '#ffffff' : '#1d1d1d';
  const specBg = t.isDark ? '#2b2b2d' : '#abb0ba';
  const K = (l, { flex = 1, bg = keyBg, fg = keyFg, fs = 19, minW } = {}) => (
    <div style={{ height: 42, borderRadius: 7, flex, minWidth: minW, background: bg, color: fg,
      display: 'flex', alignItems: 'center', justifyContent: 'center', fontSize: fs, fontFamily: 'Roboto, system-ui',
      boxShadow: t.isDark ? 'none' : '0 1px 0 rgba(0,0,0,0.18)' }}>{l}</div>
  );
  const Row = (keys, pad = 0) => (
    <div style={{ display: 'flex', gap: 6, padding: `0 ${pad}px` }}>{keys.map((l, i) => <React.Fragment key={i}>{K(l)}</React.Fragment>)}</div>
  );
  return (
    <div style={{ background: kbBg, padding: '7px 5px 10px', display: 'flex', flexDirection: 'column', gap: 8 }}>
      <div style={{ display: 'flex', justifyContent: 'space-around', padding: '1px 8px 5px', color: t.sub, fontSize: 13.5, fontWeight: 500 }}>
        <span>завтра</span><span style={{ color: t.text }}>сегодня 18:00</span><span>!важно</span>
      </div>
      {Row(['й','ц','у','к','е','н','г','ш','щ','з','х'])}
      {Row(['ф','ы','в','а','п','р','о','л','д','ж','э'], 0)}
      <div style={{ display: 'flex', gap: 6 }}>
        {K('⇧', { flex: 1.4, bg: specBg })}
        {['я','ч','с','м','и','т','ь','б','ю'].map((l, i) => <React.Fragment key={i}>{K(l)}</React.Fragment>)}
        {K('⌫', { flex: 1.4, bg: specBg })}
      </div>
      <div style={{ display: 'flex', gap: 6 }}>
        {K('?123', { flex: 1.6, bg: specBg, fs: 13 })}
        {K(',', { flex: 0.9, bg: specBg })}
        {K('пробел', { flex: 5, fs: 14 })}
        {K('.', { flex: 0.9, bg: specBg })}
        {K(accentKey || '↵', { flex: 1.6, bg: t.accent, fg: t.accentFg, fs: 16 })}
      </div>
    </div>
  );
}

// ── Строка-свойство в редакторе ───────────────────────────────────────────
function MMetaRow({ icon: I, iconColor, label, value, valueColor, t, onClick, last }) {
  return (
    <button onClick={onClick} style={{
      width: '100%', display: 'flex', alignItems: 'center', gap: 14, height: 54, padding: '0 18px',
      background: 'transparent', border: 'none', borderBottom: last ? 'none' : `1px solid ${t.hairline}`,
      cursor: 'pointer', fontFamily: M_FONT,
    }}>
      <I size={20} color={iconColor || t.sub} />
      <span style={{ flex: 1, textAlign: 'left', fontSize: 15, color: t.sub, fontWeight: 500 }}>{label}</span>
      <span style={{ fontSize: 14.5, color: valueColor || t.text, fontWeight: 600, display: 'flex', alignItems: 'center', gap: 6 }}>{value}</span>
      <ChevronRightIcon size={16} color={t.faint} />
    </button>
  );
}

// ── Полноэкранный редактор задачи ─────────────────────────────────────────
function MEditorScreen({ t, d, task, onClose }) {
  const projects = window.M_PROJECTS || [];
  const [done, setDone] = useStateD(!!(task && task.done));
  const [subs, setSubs] = useStateD(window.M_SUBTASKS || []);
  const [due, setDue] = useStateD(task && task.due ? task.due : 'Сегодня');
  const [dueDay, setDueDay] = useStateD(task && task.dueDay ? task.dueDay : 8);
  const [priority, setPriority] = useStateD(task ? task.priority : 4);
  const [projectId, setProjectId] = useStateD((task && task.project) || 'opentodo');
  const [labels, setLabels] = useStateD((task && task.labels) ? [...task.labels] : []);
  const [reminder, setReminder] = useStateD('За 1 час');
  const [repeat, setRepeat] = useStateD(task && task.recurring ? 'Каждую неделю' : 'Не повторять');
  const [sheet, setSheet] = useStateD(null);
  const [comments, setComments] = useStateD(window.M_COMMENTS || []);
  const [cText, setCText] = useStateD('');
  const [showAct, setShowAct] = useStateD(false);

  const pc = M_PRIORITY_COLOR(t, priority);
  const proj = projects.find(p => p.id === projectId) || projects[0];
  const marks = {}; (window.M_TASKS || []).forEach(x => { if (x.dueDay) (marks[x.dueDay] = marks[x.dueDay] || []).push(M_PRIORITY_COLOR(t, x.priority)); });

  const addComment = () => { if (!cText.trim()) return; setComments(cs => [...cs, { id: 'cn' + Date.now(), who: 'Денис', initials: 'Д', text: cText.trim(), when: 'только что' }]); setCText(''); };

  return (
    <div style={{ position: 'absolute', inset: 0, zIndex: 60, background: t.surface, display: 'flex', flexDirection: 'column', animation: 'mSlideUp .28s cubic-bezier(.2,.8,.2,1)' }}>
      {/* Тулбар */}
      <div style={{ paddingTop: 2 }}>
        <div style={{ height: 56, display: 'flex', alignItems: 'center', padding: '0 8px', gap: 4 }}>
          <button aria-label="Закрыть" onClick={onClose} style={iconBtnM(t)}><ChevronDownIcon size={24} color={t.text} /></button>
          <div style={{ flex: 1 }} />
          <button aria-label="Приоритет" onClick={() => setSheet('priority')} style={iconBtnM(t)}><FlagIcon size={20} color={pc} /></button>
          <button aria-label="Закрепить" style={iconBtnM(t)}><PinIcon size={19} color={t.sub} /></button>
          <button aria-label="Ещё" style={iconBtnM(t)}><MoreIcon size={21} color={t.text} /></button>
        </div>
      </div>

      <div style={{ flex: 1, overflow: 'auto', paddingBottom: 88 }}>
        {/* Заголовок + чекбокс */}
        <div style={{ display: 'flex', gap: 14, padding: '6px 18px 16px', alignItems: 'flex-start' }}>
          <button aria-label="Выполнить" onClick={() => setDone(v => !v)} style={{ width: 26, height: 26, borderRadius: 13, border: `2px solid ${done ? t.success : pc}`, background: done ? t.success : 'transparent', display: 'flex', alignItems: 'center', justifyContent: 'center', flexShrink: 0, marginTop: 3, cursor: 'pointer' }}>
            {done && <CheckIcon size={14} color="#fff" stroke={2.4} />}
          </button>
          <div style={{ flex: 1, minWidth: 0 }}>
            <div style={{ fontSize: 21, fontWeight: 700, color: done ? t.sub : t.text, lineHeight: 1.3, textDecoration: done ? 'line-through' : 'none' }}>
              {task ? task.text : 'Новая задача'}
            </div>
            <div style={{ fontSize: 14, color: t.sub, marginTop: 10, lineHeight: 1.5 }}>
              Текущая мобильная версия — ужатый десктоп: строки 40px, всё мелкое и тяжело тыкается. Нужен нормальный Android-лейаут с крупными тач-зонами и жестами.
            </div>
          </div>
        </div>

        {/* Свойства */}
        <div style={{ background: t.bg, margin: '0 0 14px', borderTop: `1px solid ${t.hairline}`, borderBottom: `1px solid ${t.hairline}` }}>
          <MMetaRow t={t} icon={CalendarIcon} iconColor={t.accent} label="Срок" value={due} valueColor={t.accent} onClick={() => setSheet('date')} />
          <MMetaRow t={t} icon={FlagIcon} iconColor={pc} label="Приоритет" value={`P${priority}`} valueColor={pc} onClick={() => setSheet('priority')} />
          <MMetaRow t={t} icon={(window[proj.icon] || HashIcon)} iconColor={proj.color} label="Проект" value={proj.name} onClick={() => setSheet('project')} />
          <MMetaRow t={t} icon={TagIcon} label="Метки" value={labels.length ? labels.join(', ') : 'Добавить'} valueColor={labels.length ? t.text : t.sub} onClick={() => setSheet('labels')} />
          <MMetaRow t={t} icon={RepeatIcon} label="Повтор" value={repeat} valueColor={repeat === 'Не повторять' ? t.sub : t.accent} onClick={() => setSheet('repeat')} />
          <MMetaRow t={t} icon={AlarmIcon} label="Напоминание" value={reminder} valueColor={reminder.startsWith('Без') ? t.sub : t.text} onClick={() => setSheet('reminder')} last />
        </div>

        {/* Подзадачи */}
        <div style={{ padding: '0 18px 18px' }}>
          <div style={{ display: 'flex', alignItems: 'center', gap: 10, marginBottom: 6 }}>
            <span style={{ fontSize: 14, fontWeight: 700, color: t.text }}>Подзадачи</span>
            <span style={{ fontSize: 12.5, color: t.sub, fontWeight: 600 }}>{subs.filter(s => s.done).length}/{subs.length}</span>
            <div style={{ flex: 1, height: 4, background: t.hairline, borderRadius: 2, overflow: 'hidden' }}>
              <div style={{ width: `${subs.filter(s => s.done).length / subs.length * 100}%`, height: '100%', background: t.accent }} />
            </div>
            <button style={{ display: 'flex', alignItems: 'center', gap: 4, background: t.accentContainer, color: t.accent, border: 'none', borderRadius: 8, padding: '5px 9px', fontSize: 12, fontWeight: 700, cursor: 'pointer', fontFamily: M_FONT }}>
              <SparklesIcon size={13} /> AI
            </button>
          </div>
          {subs.map(s => (
            <div key={s.id} style={{ display: 'flex', alignItems: 'center', gap: 12, padding: '11px 2px', borderBottom: `1px solid ${t.hairline}` }}>
              <button onClick={() => setSubs(arr => arr.map(x => x.id === s.id ? { ...x, done: !x.done } : x))} style={{ width: 21, height: 21, borderRadius: 11, border: `2px solid ${s.done ? t.success : t.sub}`, background: s.done ? t.success : 'transparent', display: 'flex', alignItems: 'center', justifyContent: 'center', flexShrink: 0, cursor: 'pointer' }}>
                {s.done && <CheckIcon size={11} color="#fff" stroke={2.4} />}
              </button>
              <span style={{ flex: 1, fontSize: 14.5, color: s.done ? t.sub : t.text, textDecoration: s.done ? 'line-through' : 'none' }}>{s.text}</span>
            </div>
          ))}
          <button style={{ display: 'flex', alignItems: 'center', gap: 9, padding: '12px 2px', background: 'transparent', border: 'none', color: t.sub, cursor: 'pointer', fontFamily: M_FONT, fontSize: 14 }}>
            <PlusIcon size={16} color={t.sub} /> Добавить подзадачу
          </button>
        </div>

        {/* Комментарии */}
        <div style={{ padding: '0 18px 10px', borderTop: `1px solid ${t.hairline}` }}>
          <div style={{ fontSize: 14, fontWeight: 700, color: t.text, padding: '14px 0 10px' }}>Комментарии</div>
          {comments.map(c => (
            <div key={c.id} style={{ display: 'flex', gap: 12, marginBottom: 14 }}>
              <span style={{ width: 32, height: 32, borderRadius: 16, flexShrink: 0, background: c.accent ? t.accent : (t.isDark ? '#444' : '#d8d8d4'), color: c.accent ? t.accentFg : t.text, display: 'flex', alignItems: 'center', justifyContent: 'center', fontSize: 13, fontWeight: 700 }}>{c.initials}</span>
              <div style={{ flex: 1, minWidth: 0 }}>
                <div style={{ display: 'flex', alignItems: 'baseline', gap: 8 }}>
                  <span style={{ fontSize: 13.5, fontWeight: 700, color: t.text }}>{c.who}</span>
                  <span style={{ fontSize: 11.5, color: t.faint }}>{c.when}</span>
                </div>
                <div style={{ fontSize: 14, color: t.text, marginTop: 3, lineHeight: 1.45 }}>{c.text}</div>
              </div>
            </div>
          ))}
          <div style={{ display: 'flex', alignItems: 'center', gap: 10, marginTop: 4 }}>
            <input value={cText} onChange={e => setCText(e.target.value)} onKeyDown={e => { if (e.key === 'Enter') addComment(); }}
              placeholder="Добавить комментарий…" style={{ flex: 1, height: 42, borderRadius: 21, border: `1px solid ${t.border}`, background: t.bg, padding: '0 16px', color: t.text, fontSize: 14, fontFamily: M_FONT, outline: 'none' }} />
            <button aria-label="Отправить" onClick={addComment} style={{ width: 42, height: 42, borderRadius: 21, border: 'none', background: cText.trim() ? t.accent : t.hover, display: 'flex', alignItems: 'center', justifyContent: 'center', cursor: 'pointer', flexShrink: 0 }}>
              <SendIcon size={17} color={cText.trim() ? t.accentFg : t.faint} />
            </button>
          </div>
        </div>

        {/* Активность */}
        <div style={{ padding: '6px 18px 8px', borderTop: `1px solid ${t.hairline}` }}>
          <button onClick={() => setShowAct(v => !v)} style={{ width: '100%', display: 'flex', alignItems: 'center', gap: 8, padding: '12px 0', background: 'transparent', border: 'none', cursor: 'pointer', fontFamily: M_FONT }}>
            <span style={{ display: 'flex', transform: showAct ? 'none' : 'rotate(-90deg)', transition: 'transform .2s' }}><ChevronDownIcon size={15} color={t.sub} /></span>
            <span style={{ fontSize: 14, fontWeight: 700, color: t.text }}>История изменений</span>
          </button>
          {showAct && (window.M_ACTIVITY || []).map(a => (
            <div key={a.id} style={{ display: 'flex', gap: 12, padding: '7px 0 7px 6px' }}>
              <span style={{ width: 7, height: 7, borderRadius: 4, background: t.faint, marginTop: 6, flexShrink: 0 }} />
              <div style={{ flex: 1 }}>
                <div style={{ fontSize: 13.5, color: t.text }}>{a.text}</div>
                <div style={{ fontSize: 11.5, color: t.faint, marginTop: 2 }}>{a.when}</div>
              </div>
            </div>
          ))}
        </div>
      </div>

      {/* Нижняя панель быстрых действий */}
      <div style={{ position: 'absolute', left: 0, right: 0, bottom: 0, height: 64, background: t.surface, borderTop: `1px solid ${t.border}`, display: 'flex', alignItems: 'center', justifyContent: 'space-around', padding: '0 12px' }}>
        {[{ I: CalendarIcon, s: 'date' }, { I: FlagIcon, s: 'priority' }, { I: TagIcon, s: 'labels' }, { I: AlarmIcon, s: 'reminder' }, { I: RepeatIcon, s: 'repeat' }].map((b, i) => (
          <button key={i} aria-label="Свойство" onClick={() => setSheet(b.s)} style={{ width: 48, height: 48, borderRadius: 24, border: 'none', background: 'transparent', display: 'flex', alignItems: 'center', justifyContent: 'center', cursor: 'pointer' }}>
            <b.I size={22} color={t.sub} />
          </button>
        ))}
      </div>

      {/* ─ Пикеры ─ */}
      <MSheet open={sheet === 'date'} onClose={() => setSheet(null)} t={t}>
        <div style={{ padding: '4px 18px 12px' }}>
          <div style={{ fontSize: 18, fontWeight: 800, color: t.text, marginBottom: 12 }}>Срок</div>
          <div style={{ display: 'flex', gap: 9, flexWrap: 'wrap' }}>
            {[{ l: 'Сегодня', day: 8, ic: TodayIcon, c: t.success }, { l: 'Завтра', day: 9, ic: UpcomingIcon, c: t.p2 }, { l: 'Выходные', day: 13, ic: CalendarIcon, c: t.p3 }, { l: 'Без срока', day: null, ic: CircleIcon, c: t.sub }].map(o => (
              <button key={o.l} onClick={() => { setDue(o.day ? o.l : 'Без срока'); setDueDay(o.day); setSheet(null); }} style={{ display: 'flex', alignItems: 'center', gap: 7, padding: '9px 14px', borderRadius: 20, border: `1px solid ${t.border}`, background: 'transparent', color: t.text, fontSize: 13.5, fontWeight: 600, cursor: 'pointer', fontFamily: M_FONT }}>
                <o.ic size={15} color={o.c} /> {o.l}
              </button>
            ))}
          </div>
        </div>
        <div style={{ padding: '0 10px 6px' }}>
          <MCalendar t={t} year={2026} month={5} marks={marks} selectedDay={dueDay} today={8}
            onPick={(dd) => { setDueDay(dd); setDue(`${dd} июня`); }} compact />
        </div>
        <button onClick={() => setSheet(null)} style={{ margin: '4px 16px 16px', width: 'calc(100% - 32px)', height: 50, borderRadius: 14, border: 'none', background: t.accent, color: t.accentFg, fontSize: 15.5, fontWeight: 700, cursor: 'pointer', fontFamily: M_FONT }}>Готово</button>
      </MSheet>

      <MOptionSheet open={sheet === 'priority'} onClose={() => setSheet(null)} t={t} title="Приоритет"
        value={String(priority)} onSelect={(id) => { setPriority(Number(id)); setSheet(null); }}
        options={[1, 2, 3, 4].map(p => ({ id: String(p), label: `Приоритет ${p}`, sub: p === 1 ? 'Срочно и важно' : p === 4 ? 'Без приоритета' : '', icon: p === 4 ? CircleIcon : FlagIcon, color: M_PRIORITY_COLOR(t, p) }))} />

      <MOptionSheet open={sheet === 'project'} onClose={() => setSheet(null)} t={t} title="Проект"
        value={projectId} onSelect={(id) => { setProjectId(id); setSheet(null); }}
        options={projects.map(p => ({ id: p.id, label: p.name, sub: `${p.count} задач`, icon: window[p.icon] || HashIcon, color: p.color }))} />

      <MOptionSheet open={sheet === 'labels'} onClose={() => setSheet(null)} t={t} title="Метки" subtitle="Можно выбрать несколько" multi
        selectedIds={labels} onSelect={(id) => setLabels(ls => ls.includes(id) ? ls.filter(x => x !== id) : [...ls, id])}
        options={(window.M_LABELS || []).map(l => ({ id: l.name, label: l.name, icon: TagIcon, color: l.color }))}
        footer={<button onClick={() => setSheet(null)} style={{ margin: '4px 16px 8px', width: 'calc(100% - 32px)', height: 50, borderRadius: 14, border: 'none', background: t.accent, color: t.accentFg, fontSize: 15.5, fontWeight: 700, cursor: 'pointer', fontFamily: M_FONT }}>Готово</button>} />

      <MOptionSheet open={sheet === 'reminder'} onClose={() => setSheet(null)} t={t} title="Напоминание"
        value={reminder} onSelect={(id) => { setReminder(id); setSheet(null); }}
        options={(window.M_REMINDER_OPTS || []).map(o => ({ id: o, label: o, icon: AlarmIcon, color: t.p2 }))} />

      <MOptionSheet open={sheet === 'repeat'} onClose={() => setSheet(null)} t={t} title="Повтор"
        value={repeat} onSelect={(id) => { setRepeat(id); setSheet(null); }}
        options={(window.M_REPEAT_OPTS || []).map(o => ({ id: o, label: o, icon: RepeatIcon, color: t.accent }))} />
    </div>
  );
}

// ── Быстрое добавление (компактное поле над клавиатурой) ──────────────────
function MQuickAdd({ open, onClose, t, onAdd }) {
  const projects = window.M_PROJECTS || [];
  const [text, setText] = useStateD('');
  const [dueState, setDueState] = useStateD('Сегодня');
  const [prio, setPrio] = useStateD(4);
  const [projIdx, setProjIdx] = useStateD(0);
  const [hasLabel, setHasLabel] = useStateD(false);
  const inputRef = React.useRef(null);

  React.useEffect(() => { if (open && inputRef.current) setTimeout(() => inputRef.current.focus(), 280); }, [open]);

  const reset = () => { setText(''); setDueState('Сегодня'); setPrio(4); setProjIdx(0); setHasLabel(false); };
  const send = () => {
    if (!text.trim()) return;
    const p = projects[projIdx] || projects[0];
    onAdd && onAdd({
      id: 'q' + Date.now(), text: text.trim(), priority: prio, project: p.id,
      due: dueState, dueKind: dueState === 'Сегодня' ? 'today' : dueState === 'Завтра' ? 'soon' : null,
      dueDay: dueState === 'Сегодня' ? 8 : dueState === 'Завтра' ? 9 : undefined,
      labels: hasLabel ? ['идея'] : [], section: 'today',
    });
    reset(); onClose();
  };

  const pc = M_PRIORITY_COLOR(t, prio);
  const proj = projects[projIdx] || projects[0];
  const chips = [
    { active: !!dueState, color: t.accent, icon: CalendarIcon, label: dueState || 'Срок', onClick: () => setDueState(s => s === 'Сегодня' ? 'Завтра' : s === 'Завтра' ? null : 'Сегодня') },
    { active: prio < 4, color: pc, icon: FlagIcon, label: 'P' + prio, onClick: () => setPrio(p => p === 1 ? 4 : p - 1) },
    { active: true, color: proj.color, icon: window[proj.icon] || HashIcon, label: proj.name, onClick: () => setProjIdx(i => (i + 1) % projects.length) },
    { active: hasLabel, color: t.p2, icon: TagIcon, label: 'идея', onClick: () => setHasLabel(v => !v) },
  ];

  return (
    <React.Fragment>
      <div onClick={onClose} style={{ position: 'absolute', inset: 0, background: t.scrim, zIndex: 44, opacity: open ? 1 : 0, pointerEvents: open ? 'auto' : 'none', transition: 'opacity .2s' }} />
      <div style={{ position: 'absolute', left: 0, right: 0, bottom: 0, zIndex: 45, transform: open ? 'translateY(0)' : 'translateY(120%)', transition: 'transform .3s cubic-bezier(.2,.8,.2,1)' }}>
        {/* Поле ввода */}
        <div style={{ background: t.surface, padding: '14px 16px 12px', borderRadius: '18px 18px 0 0', boxShadow: '0 -8px 30px rgba(0,0,0,0.25)' }}>
          <div style={{ display: 'flex', alignItems: 'flex-start', gap: 12 }}>
            <span style={{ width: 22, height: 22, borderRadius: 11, border: `2px solid ${pc}`, flexShrink: 0, marginTop: 5 }} />
            <input ref={inputRef} value={text} onChange={e => setText(e.target.value)} onKeyDown={e => { if (e.key === 'Enter') send(); }}
              placeholder="Новая задача…" style={{ flex: 1, height: 32, border: 'none', outline: 'none', background: 'transparent', color: t.text, fontSize: 17, fontWeight: 500, fontFamily: M_FONT }} />
            <button aria-label="Добавить" onClick={send} style={{ width: 38, height: 38, borderRadius: 19, background: text.trim() ? t.accent : t.hover, border: 'none', display: 'flex', alignItems: 'center', justifyContent: 'center', cursor: 'pointer', flexShrink: 0 }}>
              <SendIcon size={18} color={text.trim() ? t.accentFg : t.faint} />
            </button>
          </div>
          {/* Быстрые чипы */}
          <div style={{ display: 'flex', gap: 9, marginTop: 14, overflow: 'auto', paddingBottom: 2 }}>
            {chips.map((c, i) => (
              <button key={i} onClick={c.onClick} style={{
                display: 'flex', alignItems: 'center', gap: 6, padding: '8px 13px', borderRadius: 20,
                border: `1px solid ${c.active ? 'transparent' : t.border}`, background: c.active ? `${c.color}1f` : 'transparent',
                color: c.active ? c.color : t.sub, fontSize: 13.5, fontWeight: 600, cursor: 'pointer', whiteSpace: 'nowrap', fontFamily: M_FONT, flexShrink: 0,
              }}>
                <c.icon size={15} color={c.active ? c.color : t.sub} /> {c.label}
              </button>
            ))}
          </div>
        </div>
        <MKeyboard t={t} accentKey="↵" />
      </div>
    </React.Fragment>
  );
}

// ── Поиск ─────────────────────────────────────────────────────────────────
function MSearchScreen({ t, d, onClose, onOpenTask, tasks }) {
  const [q, setQ] = useStateD('');
  const results = q ? tasks.filter(x => x.text.toLowerCase().includes(q.toLowerCase())) : [];
  const recent = ['краш на android', 'релиз', 'копирайт'];
  return (
    <div style={{ position: 'absolute', inset: 0, zIndex: 58, background: t.bg, display: 'flex', flexDirection: 'column' }}>
      <div style={{ paddingTop: 2 }}>
        <div style={{ height: 60, display: 'flex', alignItems: 'center', gap: 8, padding: '0 12px' }}>
          <button onClick={onClose} style={iconBtnM(t)}><ChevronRightIcon size={22} color={t.text} style={{ transform: 'rotate(180deg)' }} /></button>
          <div style={{ flex: 1, display: 'flex', alignItems: 'center', gap: 10, background: t.surface, borderRadius: 24, padding: '0 16px', height: 46, border: `1px solid ${t.border}` }}>
            <SearchIcon size={19} color={t.sub} />
            <input autoFocus value={q} onChange={e => setQ(e.target.value)} placeholder="Поиск задач, проектов, меток"
              style={{ flex: 1, border: 'none', outline: 'none', background: 'transparent', color: t.text, fontSize: 15.5, fontFamily: M_FONT }} />
            {q && <button onClick={() => setQ('')} style={{ background: 'none', border: 'none', cursor: 'pointer', padding: 0 }}><div style={{ width: 20, height: 20, borderRadius: 10, background: t.border, display: 'flex', alignItems: 'center', justifyContent: 'center', color: t.sub, fontSize: 13 }}>×</div></button>}
          </div>
        </div>
      </div>
      <div style={{ flex: 1, overflow: 'auto' }}>
        {!q && (
          <div style={{ padding: '8px 6px' }}>
            <div style={{ fontSize: 12.5, fontWeight: 700, color: t.sub, padding: '10px 16px 4px', letterSpacing: 0.3, textTransform: 'uppercase' }}>Недавнее</div>
            {recent.map(r => (
              <button key={r} onClick={() => setQ(r)} style={{ width: '100%', display: 'flex', alignItems: 'center', gap: 14, padding: '13px 16px', background: 'transparent', border: 'none', cursor: 'pointer', fontFamily: M_FONT }}>
                <ClockIcon size={19} color={t.sub} />
                <span style={{ flex: 1, textAlign: 'left', fontSize: 15, color: t.text }}>{r}</span>
                <ChevronRightIcon size={16} color={t.faint} style={{ transform: 'rotate(-45deg)' }} />
              </button>
            ))}
            <div style={{ fontSize: 12.5, fontWeight: 700, color: t.sub, padding: '16px 16px 4px', letterSpacing: 0.3, textTransform: 'uppercase' }}>Фильтры</div>
            <div style={{ display: 'flex', gap: 9, flexWrap: 'wrap', padding: '4px 16px' }}>
              {[{ i: FlameIcon, l: 'Просроченные', c: t.error }, { i: TodayIcon, l: 'Сегодня', c: t.accent }, { i: StarIcon, l: 'Приоритет P1', c: t.p1 }, { i: TagIcon, l: 'баг', c: t.error }].map((f, i) => (
                <span key={i} style={{ display: 'flex', alignItems: 'center', gap: 7, padding: '9px 14px', borderRadius: 20, background: t.surface, border: `1px solid ${t.border}`, fontSize: 13.5, fontWeight: 600, color: t.text }}>
                  <f.i size={15} color={f.c} /> {f.l}
                </span>
              ))}
            </div>
          </div>
        )}
        {q && results.length === 0 && (
          <div style={{ textAlign: 'center', color: t.sub, padding: '60px 24px', fontSize: 15 }}>Ничего не найдено по «{q}»</div>
        )}
        {q && results.map(task => (
          <MTaskRow key={task.id} task={task} t={t} d={d} showProject onToggle={() => {}} onOpen={() => onOpenTask(task)} onSnooze={() => {}} onDelete={() => {}} />
        ))}
      </div>
    </div>
  );
}

// ── Настройки ─────────────────────────────────────────────────────────────
function MSettingRow({ icon: I, iconColor, label, value, t, control, last }) {
  return (
    <div style={{ display: 'flex', alignItems: 'center', gap: 14, minHeight: 56, padding: '8px 18px', borderBottom: last ? 'none' : `1px solid ${t.hairline}`, fontFamily: M_FONT }}>
      <div style={{ width: 34, height: 34, borderRadius: 10, background: `${iconColor || t.sub}1f`, display: 'flex', alignItems: 'center', justifyContent: 'center', flexShrink: 0 }}>
        <I size={18} color={iconColor || t.sub} />
      </div>
      <div style={{ flex: 1, minWidth: 0 }}>
        <div style={{ fontSize: 15, color: t.text, fontWeight: 600 }}>{label}</div>
        {value && <div style={{ fontSize: 12.5, color: t.sub, marginTop: 2 }}>{value}</div>}
      </div>
      {control}
    </div>
  );
}

function MToggle({ on, t, onClick }) {
  return (
    <button onClick={onClick} style={{ width: 50, height: 30, borderRadius: 15, border: 'none', cursor: 'pointer', background: on ? t.accent : (t.isDark ? '#4a4a4a' : '#d4d4d0'), position: 'relative', transition: 'background .2s', flexShrink: 0, padding: 0 }}>
      <span style={{ position: 'absolute', top: 3, left: on ? 23 : 3, width: 24, height: 24, borderRadius: 12, background: '#fff', transition: 'left .2s', boxShadow: '0 1px 3px rgba(0,0,0,0.3)' }} />
    </button>
  );
}

function MSettingsScreen({ t, onMenu, themeName, onTheme, density, onDensity, accent, onAccent, bigFont, onBigFont }) {
  const [push, setPush] = useStateD(true);
  const [sync, setSync] = useStateD(false);
  const list = window.M_THEME_LIST || [];
  const accents = window.M_ACCENTS || [];
  return (
    <div style={{ display: 'flex', flexDirection: 'column', height: '100%', background: t.bg }}>
      <MTopBar t={t} big onMenu={onMenu} title="Настройки" titleIcon={SettingsIcon} titleColor={t.sub} />
      <div style={{ flex: 1, overflow: 'auto', paddingBottom: 96 }}>
        {/* Внешний вид */}
        <div style={{ fontSize: 12.5, fontWeight: 700, color: t.sub, padding: '8px 20px 8px', letterSpacing: 0.3, textTransform: 'uppercase' }}>Оформление</div>
        <div style={{ background: t.surface, margin: '0 12px 16px', borderRadius: 16, overflow: 'hidden', border: `1px solid ${t.border}` }}>
          <div style={{ padding: '14px 16px', borderBottom: `1px solid ${t.hairline}` }}>
            <div style={{ fontSize: 15, fontWeight: 600, color: t.text, marginBottom: 12 }}>Тема</div>
            <div style={{ display: 'grid', gridTemplateColumns: 'repeat(3,1fr)', gap: 10 }}>
              {list.map(o => {
                const th = (window.M_THEMES || {})[o.key];
                const on = themeName === o.key;
                return (
                  <button key={o.key} onClick={() => onTheme(o.key)} style={{ borderRadius: 12, border: `2px solid ${on ? t.accent : t.border}`, background: 'transparent', cursor: 'pointer', padding: 4, fontFamily: M_FONT }}>
                    <div style={{ height: 46, borderRadius: 8, background: th.bg, display: 'flex', flexDirection: 'column', justifyContent: 'center', padding: 8, gap: 4 }}>
                      <span style={{ height: 5, width: '65%', borderRadius: 3, background: th.accent }} />
                      <span style={{ height: 4, width: '90%', borderRadius: 3, background: th.text, opacity: 0.4 }} />
                      <span style={{ height: 4, width: '50%', borderRadius: 3, background: th.text, opacity: 0.22 }} />
                    </div>
                    <div style={{ fontSize: 11.5, fontWeight: 600, color: on ? t.accent : t.sub, padding: '6px 0 2px' }}>{o.label}</div>
                  </button>
                );
              })}
            </div>
          </div>
          <div style={{ padding: '14px 16px', borderBottom: `1px solid ${t.hairline}` }}>
            <div style={{ fontSize: 15, fontWeight: 600, color: t.text, marginBottom: 12 }}>Акцент</div>
            <div style={{ display: 'flex', gap: 11, flexWrap: 'wrap' }}>
              {accents.map(a => {
                const on = (accent || t.accent) === a;
                return (
                  <button key={a} aria-label={'Акцент ' + a} onClick={() => onAccent && onAccent(a)} style={{
                    width: 34, height: 34, borderRadius: 17, background: a, border: on ? `2px solid ${t.text}` : '2px solid transparent',
                    boxShadow: `0 0 0 2px ${on ? t.surface : 'transparent'} inset`, cursor: 'pointer', display: 'flex', alignItems: 'center', justifyContent: 'center',
                  }}>
                    {on && <CheckIcon size={16} color="#fff" stroke={2.6} />}
                  </button>
                );
              })}
            </div>
          </div>
          <div style={{ padding: '14px 16px' }}>
            <div style={{ fontSize: 15, fontWeight: 600, color: t.text, marginBottom: 12 }}>Плотность списка</div>
            <MSeg t={t} value={density} onChange={onDensity}
              options={[{ value: 'spacious', label: 'Просторно' }, { value: 'balanced', label: 'Компактнее' }]} />
          </div>
        </div>

        {/* Доступность */}
        <div style={{ fontSize: 12.5, fontWeight: 700, color: t.sub, padding: '8px 20px 8px', letterSpacing: 0.3, textTransform: 'uppercase' }}>Доступность</div>
        <div style={{ background: t.surface, margin: '0 12px 16px', borderRadius: 16, overflow: 'hidden', border: `1px solid ${t.border}` }}>
          <MSettingRow t={t} icon={BookIcon} iconColor={t.p3} label="Крупный шрифт" value="Увеличить размер текста" control={<MToggle t={t} on={!!bigFont} onClick={() => onBigFont && onBigFont(!bigFont)} />} />
          <MSettingRow t={t} icon={WarningIcon} iconColor={t.p2} label="Высокий контраст" value="Тема для слабого зрения" control={<MToggle t={t} on={themeName === 'highcontrast'} onClick={() => onTheme(themeName === 'highcontrast' ? 'dark' : 'highcontrast')} />} last />
        </div>

        {/* Уведомления */}
        <div style={{ fontSize: 12.5, fontWeight: 700, color: t.sub, padding: '8px 20px 8px', letterSpacing: 0.3, textTransform: 'uppercase' }}>Уведомления и данные</div>
        <div style={{ background: t.surface, margin: '0 12px 16px', borderRadius: 16, overflow: 'hidden', border: `1px solid ${t.border}` }}>
          <MSettingRow t={t} icon={BellIcon} iconColor={t.p2} label="Пуш-уведомления" value="Напоминания о сроках" control={<MToggle t={t} on={push} onClick={() => setPush(v => !v)} />} />
          <MSettingRow t={t} icon={SyncIcon} iconColor={t.p3} label="Облачная синхронизация" value="Сейчас офлайн" control={<MToggle t={t} on={sync} onClick={() => setSync(v => !v)} />} />
          <MSettingRow t={t} icon={SparklesIcon} iconColor={t.accent} label="AI-помощник" value="Разбивка задач, подсказки" control={<ChevronRightIcon size={18} color={t.faint} />} last />
        </div>

        {/* О приложении */}
        <div style={{ fontSize: 12.5, fontWeight: 700, color: t.sub, padding: '8px 20px 8px', letterSpacing: 0.3, textTransform: 'uppercase' }}>Прочее</div>
        <div style={{ background: t.surface, margin: '0 12px 16px', borderRadius: 16, overflow: 'hidden', border: `1px solid ${t.border}` }}>
          <MSettingRow t={t} icon={HeartIcon} iconColor={t.accent} label="Поддержать разработчика" control={<ChevronRightIcon size={18} color={t.faint} />} />
          <MSettingRow t={t} icon={BookIcon} iconColor={t.sub} label="О приложении" value="OpenTodo 2.4.0" control={<ChevronRightIcon size={18} color={t.faint} />} last />
        </div>
      </div>
    </div>
  );
}

Object.assign(window, { MKeyboard, MEditorScreen, MQuickAdd, MSearchScreen, MSettingsScreen, MMetaRow, MToggle });
