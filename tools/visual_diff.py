#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Попиксельное сравнение дизайн-проекта и QML-приложения.

Три фазы (любую можно пропустить, дав готовый PNG):
  1) RENDER DESIGN — генерирует временный .html, подключающий jsx из
     .design-temp/opentodo/project/, и рендерит один артборд через
     Chrome headless → PNG фиксированного размера артборда.
  2) CAPTURE APP   — снимает окно OpenTodo.exe через Win32 PrintWindow
     (PowerShell + .NET) → PNG client-area окна.
  3) DIFF          — масштабирует оба PNG к общей сетке, считает per-pixel
     euclidean RGB distance, рисует diff-изображение и печатает метрики.

Запуск:
    # Полный прогон — рендерим артборд, снимаем окно, дифим
    python tools/visual_diff.py --artboard project --theme dark

    # С готовыми PNG (только diff)
    python tools/visual_diff.py --design design.png --app app.png --out diff.png

    # Сохранить промежуточные файлы для отладки
    python tools/visual_diff.py --artboard project --keep

Зависимости: Pillow, numpy (есть в системе), Chrome/Edge (для рендера),
запущенный OpenTodo.exe (для capture).

Артборды (см. OpenTodo Redesign.html): project, inbox, productivity,
notifications, assistant, settings, board.
Темы:   dark, kale, blueberry, graphite, light.
"""

from __future__ import annotations

import argparse
import http.server
import json
import shutil
import socketserver
import subprocess
import sys
import tempfile
import threading
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DESIGN_DIR = ROOT / ".design-temp" / "opentodo" / "project"

# Размеры артбордов из OpenTodo Redesign.html (DCArtboard width/height).
ARTBOARDS: dict[str, tuple[int, int]] = {
    "project":       (1280, 800),
    "inbox":         (1280, 800),
    "productivity":  (1280, 800),
    "notifications": (1280, 800),
    "assistant":     (1280, 800),
    "settings":      (1280, 1100),
    "board":         (1700, 900),
}

# Известные пути к Chrome / Edge на Windows.
CHROME_CANDIDATES = [
    r"C:\Program Files\Google\Chrome\Application\chrome.exe",
    r"C:\Program Files (x86)\Google\Chrome\Application\chrome.exe",
    r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
    r"C:\Program Files\Microsoft\Edge\Application\msedge.exe",
]


def _find_browser() -> Path:
    for c in CHROME_CANDIDATES:
        p = Path(c)
        if p.exists():
            return p
    raise SystemExit("Не нашёл Chrome/Edge — нужны для рендера дизайна.")


# ─── render: один артборд → PNG ──────────────────────────────────────────────

def _build_render_html(artboard: str, theme: str, w: int, h: int) -> str:
    """Собирает temp HTML, который рендерит один артборд из дизайна.

    Подключаем jsx через src="..." (так же как оригинальный
    OpenTodo Redesign.html). HTTP-сервер обходит file:// CORS-ограничения.
    """
    return f"""<!DOCTYPE html>
<html lang="ru">
<head>
  <meta charset="UTF-8" />
  <title>visual_diff render: {artboard}</title>
  <style>
    @import url('https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700&display=swap');
    html, body {{ margin:0; padding:0; height:{h}px; width:{w}px;
                  font-family:'Inter',-apple-system,system-ui,sans-serif;
                  background:#0f0f10; }}
    * {{ box-sizing: border-box; }}
    button {{ font-family: inherit; }}
    code {{ font-family: 'JetBrains Mono', ui-monospace, Menlo, monospace; font-size:11px; }}
  </style>
  <script src="https://unpkg.com/react@18.3.1/umd/react.development.js"></script>
  <script src="https://unpkg.com/react-dom@18.3.1/umd/react-dom.development.js"></script>
  <script src="https://unpkg.com/@babel/standalone@7.29.0/babel.min.js"></script>
  <script>
    // Global error trap — если babel выбросит исключение во время
    // транспиляции, оно попадёт в body для визуальной диагностики.
    window.addEventListener('error', (e) => {{
      const m = document.createElement('pre');
      m.style.cssText = 'color:#fff;background:#7a1010;padding:16px;font-size:11px;white-space:pre-wrap;';
      m.textContent = 'JS ERROR: ' + (e.message || e) + '\n' +
                      (e.filename || '') + ':' + (e.lineno || '') + ':' + (e.colno || '') +
                      '\n' + (e.error && e.error.stack || '');
      document.body.appendChild(m);
    }});
  </script>
</head>
<body>
  <div id="root"></div>
  <div id="vd-status" style="position:fixed;top:4px;right:4px;z-index:99;
       background:#333;color:#fff;font:11px monospace;padding:4px 8px;
       border-radius:3px;">loading…</div>

  <script type="text/babel" src="icons.jsx"></script>
  <script type="text/babel" src="project-icons.jsx"></script>
  <script type="text/babel" src="app-shell.jsx"></script>
  <script type="text/babel" src="view-project.jsx"></script>
  <script type="text/babel" src="view-others.jsx"></script>

  <script type="text/babel">
    const themes = {{
      dark: {{
        bg: '#1f1f1f', surface: '#282828', sidebar: '#282828', sidebarActive: '#3d2020',
        titleBar: '#2a1d1d',
        border: '#363636', text: '#e0e0e0', sidebarText: '#e0e0e0',
        subText: '#999999', iconColor: '#aaaaaa',
        accent: '#db4c3f', errorBadge: '#e05050',
        font: "'Inter', sans-serif",
      }},
      kale: {{
        bg: '#3b4a3e', surface: '#344438', sidebar: '#2d3930', sidebarActive: '#5a6f5d',
        titleBar: '#283228',
        border: '#4a5e4d', text: '#d8e8d6', sidebarText: '#c8d8c6',
        subText: '#8ea88c', iconColor: '#9ec09a',
        accent: '#7fba7a', errorBadge: '#e05050',
        font: "'Inter', sans-serif",
      }},
      blueberry: {{
        bg: '#1a1d3b', surface: '#1e2145', sidebar: '#151836', sidebarActive: '#3a3e7e',
        titleBar: '#131635',
        border: '#2a2e5e', text: '#d0d4f0', sidebarText: '#b8bce8',
        subText: '#8a90c8', iconColor: '#9aa0d0',
        accent: '#6c88f4', errorBadge: '#e05050',
        font: "'Inter', sans-serif",
      }},
      graphite: {{
        bg: '#282828', surface: '#2e2e2e', sidebar: '#1f1f1f', sidebarActive: '#3a3a3a',
        titleBar: '#1c1c1c',
        border: '#3a3a3a', text: '#d0d0d0', sidebarText: '#b0b0b0',
        subText: '#808080', iconColor: '#a0a0a0',
        accent: '#7a9cc6', errorBadge: '#e05050',
        font: "'Inter', sans-serif",
      }},
      light: {{
        bg: '#fafafa', surface: '#ffffff', sidebar: '#ffffff', sidebarActive: '#ffede8',
        titleBar: '#f4f4f4',
        border: '#eaeaea', text: '#202020', sidebarText: '#333333',
        subText: '#808080', iconColor: '#555555',
        accent: '#db4c3f', errorBadge: '#e05050',
      }},
    }};
    const theme    = themes['{theme}'] || themes.dark;
    const artboard = '{artboard}';

    // FullApp — копия из OpenTodo Redesign.html (там определена inline).
    // Здесь нужна как entry point для каждого артборда.
    function FullApp({{ activeView, theme: t }}) {{
      const view = (() => {{
        switch (activeView) {{
          case 'project':       return <ProjectView theme={{t}} />;
          case 'inbox':         return <InboxView theme={{t}} />;
          case 'productivity':  return <ProductivityView theme={{t}} />;
          case 'notifications': return <NotificationsView theme={{t}} />;
          case 'assistant':     return <AssistantView theme={{t}} />;
          case 'settings':      return <SettingsView theme={{t}} />;
          case 'board':         return <BoardView theme={{t}} />;
          default:              return <ProjectView theme={{t}} />;
        }}
      }})();

      const headerByView = {{
        project:       {{ title: 'OpenTodo',       icon: null,            sidebarActive: 'opentodo' }},
        inbox:         {{ title: 'Входящие',       icon: InboxIcon,       sidebarActive: 'inbox' }},
        productivity:  {{ title: 'Продуктивность', icon: ProductivityIcon,sidebarActive: 'productivity' }},
        notifications: {{ title: 'Уведомления',    icon: BellIcon,        sidebarActive: 'notifications' }},
        assistant:     {{ title: 'Помощник',       icon: SparklesIcon,    sidebarActive: 'assistant' }},
        settings:      {{ title: 'Настройки',      icon: SettingsIcon,    sidebarActive: 'settings' }},
        board:         {{ title: 'OpenTodo',       icon: OpenTodoMark,    sidebarActive: 'opentodo' }},
      }};
      const hdr = headerByView[activeView] || headerByView.project;

      return (
        <div style={{ {{ display:'flex', flexDirection:'column', height:'100%' }} }}>
          <div style={{ {{
            height:48, background:t.bg, borderBottom:`1px solid ${{t.border}}`,
            display:'flex', alignItems:'center', padding:'0 16px', gap:14, flexShrink:0,
          }} }}>
            <button style={{ iconBtn(t) }}><MenuIcon size={{18}} color={{t.iconColor}} /></button>
            <div style={{ {{ display:'flex', alignItems:'center', gap:8, color:t.text }} }}>
              {{hdr.icon && <hdr.icon size={{18}} color={{t.iconColor}} />}}
              <span style={{ {{ fontSize:14, fontWeight:500 }} }}>{{hdr.title}}</span>
            </div>
            <div style={{ {{ flex:1 }} }} />
            <div style={{ {{
              width:240, height:28, borderRadius:6,
              background:t.surface, border:`1px solid ${{t.border}}`,
              display:'flex', alignItems:'center', padding:'0 10px', gap:8,
            }} }}>
              <SearchIcon size={{12}} color={{t.subText}} />
              <span style={{ {{ color:t.subText, fontSize:12 }} }}>Поиск</span>
            </div>
            <div style={{ {{ display:'flex', alignItems:'center', gap:6, color:t.subText, fontSize:12 }} }}>
              <OfflineDotIcon size={{10}} color={{t.subText}} />
              <span>Офлайн</span>
            </div>
          </div>
          <div style={{ {{ flex:1, display:'flex', minHeight:0 }} }}>
            <Sidebar theme={{t}} active={{hdr.sidebarActive}} />
            {{view}}
          </div>
        </div>
      );
    }}

    function Root() {{
      return (
        <WindowChrome width={{ {w} }} height={{ {h} }} theme={{theme}}>
          <FullApp activeView={{artboard}} theme={{theme}} />
        </WindowChrome>
      );
    }}

    // Babel-standalone транспилирует src=... асинхронно (fetch+eval по очереди).
    // Не знаем когда все 5 jsx обработаны → poll'им пока ключевые глобалы
    // (WindowChrome, FullApp) не появятся, и только тогда монтируем.
    function tryMount(attempt) {{
      // Ждём пока все ключевые компоненты из jsx-файлов смонтируются на window.
      const ready = (typeof WindowChrome !== 'undefined')
                 && (typeof Sidebar !== 'undefined')
                 && (typeof ProjectView !== 'undefined')
                 && (typeof InboxView !== 'undefined');
      const statusEl = document.getElementById('vd-status');
      if (statusEl) statusEl.textContent = 'attempt ' + attempt
        + ' / WindowChrome=' + (typeof WindowChrome)
        + ' Sidebar=' + (typeof Sidebar)
        + ' ProjectView=' + (typeof ProjectView);
      if (!ready) {{
        if (attempt < 100) {{
          setTimeout(() => tryMount(attempt + 1), 100);
          return;
        }}
        if (statusEl) statusEl.style.background = '#7a1010';
        return;
      }}
      try {{
        if (statusEl) statusEl.remove();
        ReactDOM.createRoot(document.getElementById('root')).render(<Root />);
      }} catch (e) {{
        document.body.innerHTML =
          '<pre style="color:red;padding:20px;background:#fff;font-size:12px">' +
          'RENDER ERROR: ' + (e.stack || e.message || e) + '</pre>';
      }}
    }}
    tryMount(1);
  </script>
</body>
</html>
"""


class _ThreadedHTTPServer(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True


def _start_http_server(serve_dir: Path) -> tuple[_ThreadedHTTPServer, int]:
    """Поднимает HTTP-сервер в фоне над serve_dir. Возвращает (server, port).

    Babel-standalone выполняет XHR к скрипт-источникам — file:// CORS
    блокирует это; HTTP-протокол отрабатывает чисто.
    """
    class Handler(http.server.SimpleHTTPRequestHandler):
        def __init__(self, *a, **kw):
            super().__init__(*a, directory=str(serve_dir), **kw)
        def log_message(self, *a, **kw):
            pass  # тишина в stderr

    server = _ThreadedHTTPServer(("127.0.0.1", 0), Handler)
    port = server.server_address[1]
    t = threading.Thread(target=server.serve_forever, daemon=True)
    t.start()
    return server, port


def render_design(artboard: str, theme: str, out_path: Path) -> None:
    """Открывает temp HTML в --app окне Edge/Chrome (без UI-хрома),
    ждёт реального времени пока babel/React смонтируются, потом снимает
    окно через тот же GDI+ путь, что и для OpenTodo. Закрывает браузер.

    Почему не --headless: babel-standalone (~2.5 MB) ленится с
    транспиляцией в headless, virtual-time-budget не помогает (babel
    использует Promise/Worker по реальному времени). Visible window
    надёжнее и работает на всех версиях.
    """
    if artboard not in ARTBOARDS:
        raise SystemExit(f"Неизвестный артборд: {artboard} (доступны: "
                         f"{', '.join(ARTBOARDS)})")
    w, h = ARTBOARDS[artboard]
    browser = _find_browser()

    temp_name = f"_render_{artboard}_{theme}.html"
    temp_html = DESIGN_DIR / temp_name
    temp_html.write_text(_build_render_html(artboard, theme, w, h),
                         encoding="utf-8")

    # HTTP-сервер обходит file:// CORS, под которым babel не может транспилировать.
    server, port = _start_http_server(DESIGN_DIR)
    url = f"http://127.0.0.1:{port}/{temp_name}"

    # --app= открывает окно без табов и адрес-бара — close размер к артборду.
    # --user-data-dir=temp — изолированный профиль, чтобы не трогать пользовательский.
    profile_dir = Path(tempfile.mkdtemp(prefix="visdiff_profile_"))
    cmd = [
        str(browser),
        f"--app={url}",
        f"--window-size={w},{h}",
        "--window-position=0,0",
        f"--user-data-dir={profile_dir}",
        "--no-first-run",
        "--no-default-browser-check",
        "--disable-extensions",
        "--disable-popup-blocking",
    ]
    proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)

    import time
    try:
        # Ждём: 4с старт окна + babel fetch/transpile + poll'инг ~10с
        # + 1с шрифты Inter с Google + render frame. Большой запас:
        # на холодном профиле Edge стартует медленнее, babel может
        # 10+ секунд транспилировать 5 jsx по очереди.
        time.sleep(22.0)

        # --app= окно наследует title из <title> страницы (мы выставляем
        # "visual_diff render: {artboard}"). Ищем по подстроке.
        capture_app_partial("visual_diff render", out_path)
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()
        server.shutdown()
        shutil.rmtree(profile_dir, ignore_errors=True)
        if not _keep_intermediate:
            try:
                temp_html.unlink()
            except FileNotFoundError:
                pass

    if not out_path.exists() or out_path.stat().st_size < 1000:
        raise SystemExit("render_design: пустой PNG из visible-browser снимка")


# ─── capture: окно OpenTodo.exe → PNG ────────────────────────────────────────

_CAPTURE_PS = r"""
param([string]$Title, [string]$OutPath, [int]$Partial = 0)
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @"
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using System.Text;
using System.Collections.Generic;

public class WinCap {
    [DllImport("user32.dll", SetLastError=true)]
    static extern IntPtr FindWindow(string c, string t);
    [DllImport("user32.dll")]
    static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")]
    static extern bool ClientToScreen(IntPtr h, ref POINT p);
    [DllImport("user32.dll")]
    static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")]
    static extern bool ShowWindow(IntPtr h, int nCmdShow);
    [DllImport("user32.dll", CharSet=CharSet.Auto)]
    static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")]
    static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")]
    static extern bool EnumWindows(EnumProc cb, IntPtr p);

    public delegate bool EnumProc(IntPtr h, IntPtr p);

    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }

    static IntPtr _found;
    static string _needle;

    public static IntPtr FindPartial(string needle) {
        _found = IntPtr.Zero; _needle = needle.ToLowerInvariant();
        EnumWindows(new EnumProc(Cb), IntPtr.Zero);
        return _found;
    }
    static bool Cb(IntPtr h, IntPtr _) {
        if (!IsWindowVisible(h)) return true;
        StringBuilder sb = new StringBuilder(512);
        GetWindowText(h, sb, sb.Capacity);
        if (sb.ToString().ToLowerInvariant().Contains(_needle)) {
            _found = h; return false;
        }
        return true;
    }

    [DllImport("user32.dll")]
    static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint nFlags);
    [DllImport("user32.dll")]
    static extern bool GetWindowRect(IntPtr h, out RECT r);

    public static void Capture(string title, string outPath, bool partial) {
        IntPtr h = partial ? FindPartial(title) : FindWindow(null, title);
        if (h == IntPtr.Zero) throw new Exception("Window not found: " + title);
        ShowWindow(h, 9); // SW_RESTORE
        System.Threading.Thread.Sleep(150);

        // С PW_CLIENTONLY snimaem client-area, поэтому Bitmap должен быть
        // размера client area (а не всего окна с title bar / border).
        RECT r; GetClientRect(h, out r);
        int w = r.R - r.L, ch = r.B - r.T;
        if (w <= 0 || ch <= 0) throw new Exception("Empty client rect: " + w + "x" + ch);

        Bitmap bmp = new Bitmap(w, ch, PixelFormat.Format32bppArgb);
        using (Graphics g = Graphics.FromImage(bmp)) {
            IntPtr hdc = g.GetHdc();
            try {
                // PW_CLIENTONLY (0x1) обрезает title bar — мы хотим только
                // client-area для честного сравнения дизайна и QML.
                // PW_RENDERFULLCONTENT (0x2) форсирует рендер
                // hardware-accelerated окон (Edge / Chromium / QtQuick).
                if (!PrintWindow(h, hdc, 0x3))
                    throw new Exception("PrintWindow failed");
            } finally {
                g.ReleaseHdc(hdc);
            }
        }
        bmp.Save(outPath, ImageFormat.Png);
    }
}
"@
[WinCap]::Capture($Title, $OutPath, [bool]$Partial)
"""


def _capture_window(title: str, out_path: Path, partial: bool = False) -> None:
    """Снимок client-area окна через PowerShell + .NET GDI+."""
    script_path = out_path.parent / "_capture.ps1"
    script_path.write_text(_CAPTURE_PS, encoding="utf-8")
    try:
        result = subprocess.run(
            ["powershell", "-NoProfile", "-NonInteractive",
             "-ExecutionPolicy", "Bypass",
             "-File", str(script_path),
             "-Title", title, "-OutPath", str(out_path),
             "-Partial", "1" if partial else "0"],
            capture_output=True, text=True, timeout=30,
        )
        if not out_path.exists() or out_path.stat().st_size < 1000:
            sys.stderr.write(
                f"PowerShell не снял окно \"{title}\" (partial={partial}).\n"
                f"  stdout: {result.stdout[:300]}\n"
                f"  stderr: {result.stderr[:500]}\n"
            )
            raise SystemExit("capture failed")
    finally:
        if not _keep_intermediate:
            script_path.unlink(missing_ok=True)


def capture_app(title: str, out_path: Path) -> None:
    """Снимок главного окна OpenTodo по точному title."""
    _capture_window(title, out_path, partial=False)


def capture_app_partial(needle: str, out_path: Path) -> None:
    """Снимок окна, в title которого встречается needle (case-insensitive)."""
    _capture_window(needle, out_path, partial=True)


# ─── diff: два PNG → diff PNG + метрики ──────────────────────────────────────

def _strip_titlebar(img, h: int = 32):
    """Обрезает верхнюю полосу высотой h пикселей (типичная высота Win11
    title bar). Edge с PW_CLIENTONLY=1 всё равно отрисует title bar, поэтому
    обрезаем вручную — иначе title bar добавляет ~3% false-mismatch к diff.
    """
    w, ih = img.size
    return img.crop((0, h, w, ih))


def diff_images(design_path: Path, app_path: Path, out_path: Path,
                threshold: int = 10, strip_titlebar: int = 32) -> dict:
    from PIL import Image  # импорт здесь — чтобы --help работал без Pillow
    import numpy as np

    img1 = Image.open(design_path).convert("RGB")
    img2 = Image.open(app_path).convert("RGB")

    # Обрезаем title bar обоих окон — он не часть UI, а часть Windows chrome.
    if strip_titlebar > 0:
        img1 = _strip_titlebar(img1, strip_titlebar)
        img2 = _strip_titlebar(img2, strip_titlebar)

    # Масштабируем к общим размерам (берём меньшие — чтобы не апскейлить).
    target_w = min(img1.size[0], img2.size[0])
    target_h = min(img1.size[1], img2.size[1])
    img1 = img1.resize((target_w, target_h), Image.LANCZOS)
    img2 = img2.resize((target_w, target_h), Image.LANCZOS)

    a1 = np.asarray(img1, dtype=np.int16)
    a2 = np.asarray(img2, dtype=np.int16)
    diff = a1 - a2
    distance = np.sqrt(np.sum(diff.astype(np.int32) ** 2, axis=2))  # H×W float

    # Базовый слой — затемнённый дизайн в grayscale, для контекста.
    grey = np.asarray(img1.convert("L"), dtype=np.uint8) // 3   # ~33%
    out = np.stack([grey, grey, grey], axis=-1).astype(np.uint8)

    # Красный там, где distance > threshold; яркость ∝ distance.
    mask = distance > threshold
    intensity = np.clip(distance[mask] * 1.5, 0, 255).astype(np.uint8)
    out[mask, 0] = np.maximum(out[mask, 0], intensity)
    out[mask, 1] //= 2
    out[mask, 2] //= 2

    Image.fromarray(out).save(out_path)

    total = target_w * target_h
    diff_px = int(mask.sum())
    return {
        "size":            f"{target_w}×{target_h}",
        "total_pixels":    total,
        "diff_pixels":     diff_px,
        "matching_pct":    round(100.0 * (1 - diff_px / total), 2),
        "mean_distance":   round(float(distance.mean()), 2),
        "max_distance":    round(float(distance.max()), 2),
        "rmse":            round(float(np.sqrt((distance ** 2).mean())), 2),
        "threshold":       threshold,
    }


# ─── CLI ──────────────────────────────────────────────────────────────────────

_keep_intermediate = False


def main() -> None:
    global _keep_intermediate

    ap = argparse.ArgumentParser(
        description="Попиксельное сравнение дизайна и QML",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument("--artboard", default="project",
                    help=f"артборд для рендера (по умолчанию project); "
                         f"варианты: {', '.join(ARTBOARDS)}")
    ap.add_argument("--theme", default="dark",
                    help="палитра (dark/kale/blueberry/graphite/light)")
    ap.add_argument("--design", type=Path,
                    help="готовый PNG дизайна (если не задан — рендерим)")
    ap.add_argument("--app", type=Path,
                    help="готовый PNG приложения (если не задан — снимаем)")
    ap.add_argument("--app-title", default="OpenTodo",
                    help="заголовок окна для capture (default: OpenTodo)")
    ap.add_argument("--out", type=Path, default=ROOT / "visual_diff.png",
                    help="куда сохранить diff PNG (default: visual_diff.png)")
    ap.add_argument("--threshold", type=int, default=10,
                    help="порог euclidean distance для пометки пикселя (default: 10)")
    ap.add_argument("--strip-titlebar", type=int, default=32,
                    help="обрезать N верхних пикселей перед diff (default: 32)")
    ap.add_argument("--keep", action="store_true",
                    help="оставить промежуточные PNG и temp HTML")
    args = ap.parse_args()

    _keep_intermediate = args.keep

    for s in (sys.stdout, sys.stderr):
        if hasattr(s, "reconfigure"):
            try: s.reconfigure(encoding="utf-8")
            except Exception: pass

    # Готовим пути к промежуточным файлам.
    tmpdir = Path(tempfile.mkdtemp(prefix="visual_diff_"))
    try:
        design_png = args.design or (tmpdir / "design.png")
        app_png    = args.app    or (tmpdir / "app.png")

        if args.design is None:
            print(f"→ render design ({args.artboard} / {args.theme}) ...")
            render_design(args.artboard, args.theme, design_png)
            print(f"  saved: {design_png}")

        if args.app is None:
            print(f"→ capture app (\"{args.app_title}\") ...")
            capture_app(args.app_title, app_png)
            print(f"  saved: {app_png}")

        print("→ diff ...")
        metrics = diff_images(design_png, app_png, args.out,
                              threshold=args.threshold,
                              strip_titlebar=args.strip_titlebar)
        print(f"  saved: {args.out}")
        print()
        print(json.dumps(metrics, indent=2, ensure_ascii=False))

        if args.keep:
            keep_dir = ROOT / "visual_diff_intermediate"
            keep_dir.mkdir(exist_ok=True)
            for src in (design_png, app_png):
                if src.exists():
                    shutil.copy2(src, keep_dir / src.name)
            print(f"\n  intermediate files: {keep_dir}")
    finally:
        if not args.keep:
            shutil.rmtree(tmpdir, ignore_errors=True)


if __name__ == "__main__":
    main()
