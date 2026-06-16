/*
 * Переиспользуемые листы и пикеры: снэкбар (Undo), опшен-лист (дата/приоритет/
 * проект/метки/напоминание/повтор), календарь-месяц, контекст-меню задачи.
 */
const { useState: useStateP } = React;

// ── Снэкбар с отменой ─────────────────────────────────────────────────────
function MSnackbar({ snack, t, bottom = 78 }) {
  const show = !!snack;
  return (
    <div style={{
      position: 'absolute', left: 12, right: 12, bottom, zIndex: 70,
      transform: show ? 'translateY(0)' : 'translateY(160%)', opacity: show ? 1 : 0,
      transition: 'transform .28s cubic-bezier(.2,.8,.2,1), opacity .2s',
      pointerEvents: show ? 'auto' : 'none',
    }}>
      <div style={{
        background: t.isDark ? '#e8e8e8' : '#2c2c2c', color: t.isDark ? '#1d1d1d' : '#fff',
        borderRadius: 13, padding: '13px 8px 13px 18px', display: 'flex', alignItems: 'center', gap: 12,
        boxShadow: '0 8px 28px rgba(0,0,0,0.32)', fontFamily: M_FONT,
      }}>
        <span style={{ flex: 1, fontSize: 14.5, fontWeight: 500 }}>{snack ? snack.msg : ''}</span>
        {snack && snack.actionLabel && (
          <button onClick={snack.onAction} style={{
            background: 'transparent', border: 'none', cursor: 'pointer', fontFamily: M_FONT,
            color: t.accent, fontSize: 14.5, fontWeight: 700, padding: '6px 14px', borderRadius: 8,
          }}>{snack.actionLabel}</button>
        )}
      </div>
    </div>
  );
}

// ── Универсальный лист выбора ─────────────────────────────────────────────
function MOptionSheet({ open, onClose, t, title, subtitle, options, value, selectedIds, multi, onSelect, footer }) {
  return (
    <MSheet open={open} onClose={onClose} t={t}>
      {(title || subtitle) && (
        <div style={{ padding: '4px 20px 10px' }}>
          {title && <div style={{ fontSize: 18, fontWeight: 800, color: t.text }}>{title}</div>}
          {subtitle && <div style={{ fontSize: 13, color: t.sub, marginTop: 4 }}>{subtitle}</div>}
        </div>
      )}
      <div style={{ padding: '2px 10px 8px', maxHeight: 380, overflow: 'auto' }}>
        {options.map(o => {
          const Ic = o.icon;
          const on = multi ? (selectedIds || []).includes(o.id) : value === o.id;
          return (
            <button key={o.id} onClick={() => onSelect(o.id)} style={{
              width: '100%', display: 'flex', alignItems: 'center', gap: 14, minHeight: 54, padding: '8px 14px',
              borderRadius: 14, border: 'none', cursor: 'pointer', fontFamily: M_FONT,
              background: on ? t.accentContainer : 'transparent',
            }}>
              {Ic && (
                <span style={{ width: 34, height: 34, borderRadius: 10, background: `${o.color || t.sub}1f`, display: 'flex', alignItems: 'center', justifyContent: 'center', flexShrink: 0 }}>
                  <Ic size={18} color={o.color || t.sub} />
                </span>
              )}
              <div style={{ flex: 1, minWidth: 0, textAlign: 'left' }}>
                <div style={{ fontSize: 15.5, fontWeight: on ? 700 : 500, color: on ? t.accent : t.text, whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>{o.label}</div>
                {o.sub && <div style={{ fontSize: 12.5, color: t.sub, marginTop: 2 }}>{o.sub}</div>}
              </div>
              {o.right}
              {on && <CheckIcon size={18} color={t.accent} stroke={2.4} />}
            </button>
          );
        })}
      </div>
      {footer}
      <div style={{ height: 12 }} />
    </MSheet>
  );
}

// ── Календарь-месяц ───────────────────────────────────────────────────────
const M_MONTHS = ['Январь', 'Февраль', 'Март', 'Апрель', 'Май', 'Июнь', 'Июль', 'Август', 'Сентябрь', 'Октябрь', 'Ноябрь', 'Декабрь'];
const M_WD = ['Пн', 'Вт', 'Ср', 'Чт', 'Пт', 'Сб', 'Вс'];

function MCalendar({ t, year, month, marks, selectedDay, today, onPick, compact }) {
  const first = new Date(year, month, 1);
  const lead = (first.getDay() + 6) % 7; // Mon-first
  const days = new Date(year, month + 1, 0).getDate();
  const cells = [];
  for (let i = 0; i < lead; i++) cells.push(null);
  for (let dd = 1; dd <= days; dd++) cells.push(dd);
  while (cells.length % 7 !== 0) cells.push(null);
  const cellH = compact ? 38 : 46;
  return (
    <div>
      <div style={{ display: 'grid', gridTemplateColumns: 'repeat(7,1fr)', padding: '0 6px 6px' }}>
        {M_WD.map(w => (
          <div key={w} style={{ textAlign: 'center', fontSize: 11.5, fontWeight: 700, color: t.sub, padding: '4px 0' }}>{w}</div>
        ))}
      </div>
      <div style={{ display: 'grid', gridTemplateColumns: 'repeat(7,1fr)' }}>
        {cells.map((dd, i) => {
          if (!dd) return <div key={i} style={{ height: cellH }} />;
          const isToday = dd === today;
          const isSel = dd === selectedDay;
          const m = (marks && marks[dd]) || [];
          return (
            <button key={i} onClick={() => onPick && onPick(dd)} style={{
              height: cellH, border: 'none', background: 'transparent', cursor: 'pointer', fontFamily: M_FONT,
              display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', gap: 3,
            }}>
              <span style={{
                width: 32, height: 32, borderRadius: 16, display: 'flex', alignItems: 'center', justifyContent: 'center',
                fontSize: 14.5, fontWeight: isToday || isSel ? 700 : 500,
                background: isSel ? t.accent : (isToday ? t.accentContainer : 'transparent'),
                color: isSel ? t.accentFg : (isToday ? t.accent : t.text),
              }}>{dd}</span>
              <span style={{ display: 'flex', gap: 3, height: 5 }}>
                {m.slice(0, 3).map((c, j) => <span key={j} style={{ width: 5, height: 5, borderRadius: 3, background: c }} />)}
              </span>
            </button>
          );
        })}
      </div>
    </div>
  );
}

// ── Контекст-меню задачи ──────────────────────────────────────────────────
function MContextSheet({ open, onClose, t, task, actions }) {
  return (
    <MSheet open={open} onClose={onClose} t={t}>
      {task && (
        <div style={{ padding: '2px 20px 12px', display: 'flex', alignItems: 'center', gap: 12, borderBottom: `1px solid ${t.hairline}` }}>
          <span style={{ width: 18, height: 18, borderRadius: 9, border: `2px solid ${M_PRIORITY_COLOR(t, task.priority)}`, flexShrink: 0 }} />
          <span style={{ fontSize: 14.5, color: t.text, fontWeight: 500, lineHeight: 1.3, display: '-webkit-box', WebkitLineClamp: 2, WebkitBoxOrient: 'vertical', overflow: 'hidden' }}>{task.text}</span>
        </div>
      )}
      <div style={{ padding: '8px 10px 10px' }}>
        {actions.map((a, i) => (
          <button key={i} onClick={a.onClick} style={{
            width: '100%', display: 'flex', alignItems: 'center', gap: 16, height: 52, padding: '0 14px',
            borderRadius: 12, border: 'none', cursor: 'pointer', background: 'transparent', fontFamily: M_FONT,
          }}>
            <a.icon size={21} color={a.color || t.sub} />
            <span style={{ fontSize: 15.5, fontWeight: 500, color: a.color || t.text }}>{a.label}</span>
          </button>
        ))}
      </div>
      <div style={{ height: 12 }} />
    </MSheet>
  );
}

Object.assign(window, { MSnackbar, MOptionSheet, MCalendar, MContextSheet, M_MONTHS, M_WD });
