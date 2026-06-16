/*
 * Общие мобильные компоненты OpenTodo.
 * Главное здесь — MTaskRow со свайп-жестами (вправо: выполнить, влево: отложить/удалить).
 */
const { useState: useStateC, useRef: useRefC, useEffect: useEffectC } = React;

// ── Круглый чекбокс с цветом приоритета ───────────────────────────────────
function MCheck({ done, priority = 4, t, d, onToggle }) {
  const c = M_PRIORITY_COLOR(t, priority);
  return (
    <button
      onClick={(e) => { e.stopPropagation(); onToggle && onToggle(); }}
      style={{
        width: d.check, height: d.check, borderRadius: d.check / 2,
        border: `2px solid ${done ? c : c}`, background: done ? c : 'transparent',
        display: 'flex', alignItems: 'center', justifyContent: 'center',
        flexShrink: 0, cursor: 'pointer', padding: 0, marginTop: 1,
        transition: 'background .15s, border-color .15s',
        position: 'relative',
      }}
    >
      {/* лёгкая подложка приоритета для незавершённых, чтобы кружок читался */}
      {!done && priority <= 3 && (
        <span style={{ position: 'absolute', inset: 3, borderRadius: '50%', background: c, opacity: 0.12 }} />
      )}
      {done && <CheckIcon size={d.checkInner} color={t.accentFg} stroke={2.4} />}
    </button>
  );
}

// ── Мета-чип задачи ───────────────────────────────────────────────────────
function MChip({ icon: I, children, color, t, d, tinted }) {
  return (
    <span style={{
      display: 'inline-flex', alignItems: 'center', gap: 5,
      height: d.chipH, padding: tinted ? '0 9px' : '0 2px', borderRadius: 7,
      background: tinted ? `${color}1f` : 'transparent',
      color: color || t.sub, fontSize: d.meta, fontWeight: 500, whiteSpace: 'nowrap',
    }}>
      {I && <I size={d.meta + 1.5} />}
      {children}
    </span>
  );
}

// ── Свайпаемая строка задачи ──────────────────────────────────────────────
const SWIPE = { complete: 92, actions: 84, openAt: 156 };

function MTaskRow({ task, t, d, onToggle, onOpen, onSnooze, onDelete, showProject, onLongPress, selectionMode, selected, onSelectToggle }) {
  const [dx, setDx] = useStateC(0);
  const [dragging, setDragging] = useStateC(false);
  const [open, setOpen] = useStateC(false);      // левые экшены раскрыты
  const [gone, setGone] = useStateC(false);      // строка выполнена и сворачивается
  const start = useRefC(null);
  const axis = useRefC(null);
  const lpTimer = useRefC(null);
  const lpFired = useRefC(false);

  const proj = (window.M_PROJECTS || []).find(p => p.id === task.project);
  // Сегодня — зелёный, скоро — оранжевый; красный только у просрочки
  const dueColor = task.dueKind === 'overdue' ? t.error
    : task.dueKind === 'today' ? t.success
    : task.dueKind === 'soon' ? t.p2 : t.sub;

  function down(e) {
    const p = e.touches ? e.touches[0] : e;
    start.current = { x: p.clientX, y: p.clientY, base: open ? -SWIPE.openAt : 0 };
    axis.current = null;
    lpFired.current = false;
    if (!selectionMode && onLongPress) {
      lpTimer.current = setTimeout(() => { lpFired.current = true; onLongPress(task); }, 480);
    }
    if (!selectionMode) setDragging(true);
  }
  function clearLP() { if (lpTimer.current) { clearTimeout(lpTimer.current); lpTimer.current = null; } }
  function move(e) {
    if (!start.current) return;
    const p = e.touches ? e.touches[0] : e;
    const ddx = p.clientX - start.current.x;
    const ddy = p.clientY - start.current.y;
    if (Math.abs(ddx) > 6 || Math.abs(ddy) > 6) clearLP();
    if (selectionMode) return;
    if (axis.current === null) {
      if (Math.abs(ddx) < 6 && Math.abs(ddy) < 6) return;
      axis.current = Math.abs(ddx) > Math.abs(ddy) ? 'x' : 'y';
    }
    if (axis.current !== 'x') return;
    if (e.cancelable) e.preventDefault();
    let nx = start.current.base + ddx;
    nx = Math.max(-SWIPE.openAt - 24, Math.min(SWIPE.complete + 40, nx));
    setDx(nx);
  }
  function up() {
    clearLP();
    if (!start.current) { setDragging(false); return; }
    setDragging(false);
    start.current = null;
    if (selectionMode || axis.current !== 'x') { return; }
    if (dx > SWIPE.complete) {
      // Выполнить
      setDx(window.innerWidth);
      setTimeout(() => { setGone(true); onToggle && onToggle(); }, 180);
      return;
    }
    if (dx < -SWIPE.actions) { setOpen(true); setDx(-SWIPE.openAt); return; }
    setOpen(false); setDx(0);
  }

  if (gone) return null;

  const progress = task.sub ? task.sub.done / task.sub.total : 0;
  const tx = selectionMode ? 0 : (dragging ? dx : (open ? -SWIPE.openAt : 0));

  return (
    <div style={{ position: 'relative', overflow: 'hidden', background: t.bg }}>
      {/* Фон-подложка: слева (при свайпе вправо) — выполнить */}
      <div style={{
        position: 'absolute', inset: 0, display: 'flex', alignItems: 'center',
        paddingLeft: 22, background: t.success,
        opacity: tx > 4 ? 1 : 0,
      }}>
        <CheckCircleIcon size={24} color="#fff" stroke={2} />
        <span style={{ color: '#fff', fontWeight: 600, marginLeft: 12, fontSize: 14 }}>Выполнить</span>
      </div>
      {/* Правые экшены: отложить + удалить */}
      <div style={{ position: 'absolute', top: 0, bottom: 0, right: 0, display: 'flex' }}>
        <button onClick={() => { setOpen(false); setDx(0); onSnooze && onSnooze(); }} style={{
          width: SWIPE.openAt - 78, border: 'none', cursor: 'pointer',
          background: t.warning, color: '#fff', display: 'flex', flexDirection: 'column',
          alignItems: 'center', justifyContent: 'center', gap: 3, fontSize: 11.5, fontWeight: 600,
        }}>
          <AlarmIcon size={20} color="#fff" /> Отложить
        </button>
        <button onClick={() => { setOpen(false); setDx(0); onDelete && onDelete(); }} style={{
          width: 78, border: 'none', cursor: 'pointer',
          background: t.error, color: '#fff', display: 'flex', flexDirection: 'column',
          alignItems: 'center', justifyContent: 'center', gap: 3, fontSize: 11.5, fontWeight: 600,
        }}>
          <TrashIcon size={20} color="#fff" /> Удалить
        </button>
      </div>

      {/* Сама строка */}
      <div
        onPointerDown={down} onPointerMove={move} onPointerUp={up} onPointerCancel={up}
        onClick={() => {
          if (lpFired.current) { lpFired.current = false; return; }
          if (selectionMode) { onSelectToggle && onSelectToggle(task.id); return; }
          if (open) { setOpen(false); setDx(0); return; }
          if (Math.abs(dx) < 4) onOpen && onOpen();
        }}
        style={{
          position: 'relative', cursor: 'pointer',
          background: selected ? t.accentContainer : t.bg,
          transform: `translateX(${tx}px)`,
          transition: dragging ? 'none' : 'transform .26s cubic-bezier(.2,.8,.2,1)',
          touchAction: 'pan-y',
          display: 'flex', alignItems: 'flex-start', gap: d.rowGap,
          padding: `${d.rowPadV}px ${d.rowPadH}px`,
          minHeight: d.rowMinH, boxSizing: 'border-box',
          borderBottom: `1px solid ${t.hairline}`,
        }}
      >
        {selectionMode ? (
          <span style={{
            width: d.check, height: d.check, borderRadius: d.check / 2, flexShrink: 0, marginTop: 1,
            border: `2px solid ${selected ? t.accent : t.faint}`, background: selected ? t.accent : 'transparent',
            display: 'flex', alignItems: 'center', justifyContent: 'center',
          }}>
            {selected && <CheckIcon size={d.checkInner} color={t.accentFg} stroke={2.4} />}
          </span>
        ) : (
          <MCheck done={task.done} priority={task.priority} t={t} d={d} onToggle={onToggle} />
        )}
        <div style={{ flex: 1, minWidth: 0 }}>
          <div style={{
            fontSize: d.title, color: task.done ? t.sub : t.text, fontWeight: 450,
            lineHeight: d.titleLh, textDecoration: task.done ? 'line-through' : 'none',
            display: '-webkit-box', WebkitLineClamp: 2, WebkitBoxOrient: 'vertical', overflow: 'hidden',
          }}>{task.text}</div>

          {(task.due || task.deadline || task.recurring || (task.labels && task.labels.length) || (showProject && proj)) && (
            <div style={{ display: 'flex', gap: 10, marginTop: 7, alignItems: 'center', flexWrap: 'wrap' }}>
              {task.recurring && <MChip icon={RepeatIcon} color={t.accent} t={t} d={d}>повтор</MChip>}
              {task.due && <MChip icon={CalendarIcon} color={dueColor} t={t} d={d}>{task.due}</MChip>}
              {task.deadline && <MChip icon={FlagIcon} color={t.error} t={t} d={d}>{task.deadline}</MChip>}
              {task.sub && (
                <MChip icon={task.sub.done === task.sub.total ? CheckCircleIcon : ListIcon}
                  color={task.sub.done === task.sub.total ? t.success : t.sub} t={t} d={d}>
                  {task.sub.done}/{task.sub.total}
                </MChip>
              )}
              {task.labels && task.labels.map(l => {
                const lab = (window.M_LABELS || []).find(x => x.name === l);
                return <MChip key={l} icon={TagIcon} color={lab ? lab.color : t.sub} t={t} d={d} tinted>{l}</MChip>;
              })}
              {showProject && proj && <MChip icon={HashIcon} color={proj.color} t={t} d={d}>{proj.name}</MChip>}
            </div>
          )}

          {task.sub && task.sub.done < task.sub.total && (
            <div style={{ height: 3, borderRadius: 2, background: t.hairline, marginTop: 9, overflow: 'hidden' }}>
              <div style={{ width: `${progress * 100}%`, height: '100%', background: t.accent, borderRadius: 2 }} />
            </div>
          )}
        </div>
      </div>
    </div>
  );
}

// ── Заголовок секции ──────────────────────────────────────────────────────
function MSection({ title, count, t, d, collapsed, onToggle }) {
  return (
    <button onClick={onToggle} style={{
      width: '100%', display: 'flex', alignItems: 'center', gap: 9,
      height: d.sectionH, padding: `0 ${d.rowPadH}px`, background: t.bg,
      border: 'none', borderBottom: `1px solid ${t.hairline}`, cursor: 'pointer',
      fontFamily: M_FONT,
    }}>
      <span style={{ display: 'flex', transform: collapsed ? 'rotate(-90deg)' : 'none', transition: 'transform .2s', color: t.sub }}>
        <ChevronDownIcon size={15} color={t.sub} />
      </span>
      <span style={{ fontSize: d.sectionFs, fontWeight: 700, color: t.text, letterSpacing: 0.2 }}>{title}</span>
      {count != null && <span style={{ fontSize: d.sectionFs - 1.5, color: t.sub, fontWeight: 600 }}>{count}</span>}
    </button>
  );
}

// ── FAB ───────────────────────────────────────────────────────────────────
function MFab({ t, onClick, bottom = 22, icon: I = PlusIcon, small }) {
  const s = small ? 48 : 60;
  return (
    <button onClick={onClick} style={{
      position: 'absolute', right: 18, bottom, width: s, height: s, borderRadius: small ? 16 : 19,
      background: t.accent, border: 'none', cursor: 'pointer', zIndex: 30,
      display: 'flex', alignItems: 'center', justifyContent: 'center',
      boxShadow: `0 8px 22px ${t.isDark ? 'rgba(0,0,0,0.5)' : 'rgba(219,76,63,0.34)'}`,
    }}>
      <I size={small ? 22 : 27} color={t.accentFg} stroke={2.2} />
    </button>
  );
}

// ── Сегмент-переключатель (вид списка) ────────────────────────────────────
function MSeg({ options, value, onChange, t }) {
  return (
    <div style={{ display: 'flex', background: t.isDark ? 'rgba(255,255,255,0.06)' : 'rgba(0,0,0,0.05)', borderRadius: 11, padding: 3, gap: 2 }}>
      {options.map(o => {
        const on = o.value === value;
        return (
          <button key={o.value} onClick={() => onChange(o.value)} style={{
            display: 'flex', alignItems: 'center', justifyContent: 'center', gap: 6,
            padding: '7px 13px', borderRadius: 9, border: 'none', cursor: 'pointer',
            background: on ? t.surface : 'transparent', color: on ? t.text : t.sub,
            fontSize: 13, fontWeight: 600, fontFamily: M_FONT,
            boxShadow: on ? (t.isDark ? '0 1px 3px rgba(0,0,0,0.4)' : '0 1px 3px rgba(0,0,0,0.1)') : 'none',
          }}>
            {o.icon && <o.icon size={15} />}{o.label}
          </button>
        );
      })}
    </div>
  );
}

// ── Нижний лист (bottom sheet) ────────────────────────────────────────────
function MSheet({ open, onClose, t, children, height, pad = true }) {
  return (
    <React.Fragment>
      <div onClick={onClose} style={{
        position: 'absolute', inset: 0, background: t.scrim, zIndex: 40,
        opacity: open ? 1 : 0, pointerEvents: open ? 'auto' : 'none', transition: 'opacity .25s',
      }} />
      <div style={{
        position: 'absolute', left: 0, right: 0, bottom: 0, zIndex: 41,
        background: t.surface, borderRadius: '22px 22px 0 0',
        transform: open ? 'translateY(0)' : 'translateY(110%)',
        transition: 'transform .3s cubic-bezier(.2,.8,.2,1)',
        boxShadow: '0 -10px 40px rgba(0,0,0,0.3)', maxHeight: '88%',
        padding: pad ? '8px 0 0' : 0, height,
      }}>
        <div style={{ width: 36, height: 4, borderRadius: 2, background: t.border, margin: '8px auto 6px' }} />
        {children}
      </div>
    </React.Fragment>
  );
}

Object.assign(window, { MCheck, MChip, MTaskRow, MSection, MFab, MSeg, MSheet, SWIPE });
