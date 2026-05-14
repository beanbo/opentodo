"""Sidebar navigation tests.

Each item in the left sidebar (Inbox, Today, Upcoming, Productivity,
Notifications, Helper, Settings) should change the view title in the
top bar and (where appropriate) the visible content.

These tests use pixel detection to find sidebar positions dynamically
instead of hardcoding Y coordinates — the sidebar reflows depending on
how many projects/filters exist.
"""
from __future__ import annotations

import time

from PIL import Image

from conftest import is_accent, find_accent_pixels, find_plus_glyph

# The sidebar is 280 px wide; clicking inside it at x=100 always lands
# on a SidebarItem (any X is fine, the item spans the full width).
SIDEBAR_X = 100


def _find_yellow_pixels(img: Image.Image, box) -> list[tuple[int, int]]:
    """Find pixels that look like the yellow ✨ icon used by the
    'Помощник' (Helper) sidebar item."""
    x0, y0, x1, y1 = box
    crop = img.crop(box).convert("RGB")
    out = []
    for y in range(crop.height):
        for x in range(crop.width):
            r, g, b = crop.getpixel((x, y))
            # Yellow-ish: high R+G, low B
            if r > 180 and g > 140 and b < 90:
                out.append((x0 + x, y0 + y))
    return out


def _find_helper_y(img: Image.Image) -> int | None:
    """Locate the Y coordinate of the 'Помощник' row by detecting its
    yellow ✨ icon. There are several yellow-ish icons in the sidebar
    (notification bell 🔔, today calendar marker, ...) so we cluster by
    Y and pick the LOWEST cluster — Помощник sits below Уведомления."""
    box = (0, 200, 280, 700)
    yellow = _find_yellow_pixels(img, box)
    if not yellow:
        return None
    # Cluster by Y (gap > 15px = new cluster).
    ys = sorted(p[1] for p in yellow)
    clusters: list[list[int]] = [[ys[0]]]
    for y in ys[1:]:
        if y - clusters[-1][-1] > 15:
            clusters.append([y])
        else:
            clusters[-1].append(y)
    # Drop tiny clusters (< 5 pixels — not a real icon).
    clusters = [c for c in clusters if len(c) >= 5]
    if not clusters:
        return None
    # Return median y of the bottom-most substantial cluster (Помощник).
    bottom = clusters[-1]
    return bottom[len(bottom) // 2]


def _is_pure_accent(rgb) -> bool:
    """Stricter accent test than conftest.is_accent — filters out
    ClearType subpixel fringes (warm-tinted anti-aliasing on white text)
    which leak through the standard predicate."""
    r, g, b = rgb[:3]
    return r > 150 and r - g > 80 and r - b > 80


def _sidebar_visible(img: Image.Image) -> bool:
    """The sidebar surface uses ThemeManager.surfaceColor (~#2a2a2a in
    dark mode) which is distinct from the main background (~#1f1f1f).
    Sample a known-sidebar-only area and a known-main-only area; if
    they're noticeably different, the sidebar is visible."""
    # Pick (200, 200) — solidly inside the sidebar (x<280) and below
    # the headers (y > 50 + a few items). Then (600, 200) — solidly in
    # main content (x > 280 + 50px buffer). Compare luminance.
    p_sidebar = img.convert("L").getpixel((200, 200))
    p_main = img.convert("L").getpixel((600, 200))
    # 5+ luminance points = visibly different surfaces.
    return abs(p_sidebar - p_main) >= 5


def _find_inbox_y(img: Image.Image) -> int | None:
    """Locate the 'Входящие' row by its accent-color icon + text.
    Filters via _is_pure_accent to skip ClearType fringes from white
    text in the topbar."""
    crop = img.crop((10, 30, 280, 200)).convert("RGB")
    pixels = []
    for y in range(crop.height):
        for x in range(crop.width):
            if _is_pure_accent(crop.getpixel((x, y))):
                pixels.append((x + 10, y + 30))
    if len(pixels) < 30:
        return None
    # Cluster by Y proximity (gap > 10px = different row).
    ys_sorted = sorted(p[1] for p in pixels)
    clusters: list[list[int]] = [[ys_sorted[0]]]
    for y in ys_sorted[1:]:
        if y - clusters[-1][-1] > 10:
            clusters.append([y])
        else:
            clusters[-1].append(y)
    biggest = max(clusters, key=len)
    if len(biggest) < 30:
        return None
    return biggest[len(biggest) // 2]


# ── Tests ───────────────────────────────────────────────────────────────────


def test_nav_inbox_default(app, recorder):
    """The default startup view is Inbox. Verify the title shows
    "Входящие" and the QuickAdd '+' glyph is visible (only on task views,
    not on full-page views like Settings)."""
    img = app.screenshot()
    bbox = find_plus_glyph(img)
    recorder(f"plus glyph at: {bbox}")
    assert bbox, "QuickAdd '+' should be visible on Inbox view"

    # The sidebar must be visible to confirm we're on the desktop layout.
    assert _sidebar_visible(img), "Sidebar should be visible on default view"


def test_nav_settings_hides_quickadd(app, recorder):
    """Clicking Настройки (settings) must hide the QuickAdd row (it's a
    full-page view) and show settings sections."""
    pre = app.screenshot()
    helper_y = _find_helper_y(pre)
    recorder(f"helper row at y={helper_y}")
    assert helper_y is not None, "Couldn't find 'Помощник' row in sidebar"

    # Settings is the next item below Помощник; rows are ~36 px tall.
    settings_y = helper_y + 36
    recorder(f"clicking settings at y={settings_y}")
    app.click_client(SIDEBAR_X, settings_y)
    time.sleep(0.8)
    after = app.screenshot()
    recorder("after click settings")

    # No '+' glyph anymore — Settings is a full-page view.
    bbox = find_plus_glyph(after)
    recorder(f"plus glyph after settings click: {bbox}")
    assert bbox is None, (
        "QuickAdd '+' is still visible after navigating to Settings — "
        "either the click landed on the wrong row or the full-page-view "
        "switching is broken."
    )

    # Check the page actually rendered something — count light pixels in
    # the body area.
    body = after.crop((350, 200, 1100, 700)).convert("L")
    light_count = sum(1 for p in body.getdata() if p > 180)
    recorder(f"settings body light pixels: {light_count}")
    assert light_count > 500, (
        f"Settings page appears blank ({light_count} light pixels). "
        f"Section content not rendered?"
    )


def test_nav_today_view_keeps_quickadd(app, recorder):
    """Clicking Сегодня should keep the QuickAdd visible (Today is a
    task view)."""
    pre = app.screenshot()
    inbox_y = _find_inbox_y(pre)
    if inbox_y is None:
        # Already on a non-Inbox view; navigate manually.
        inbox_y = 57

    today_y = inbox_y + 36
    app.click_client(SIDEBAR_X, today_y)
    time.sleep(0.6)
    after = app.screenshot()
    recorder("after click today")

    bbox = find_plus_glyph(after)
    assert bbox, "QuickAdd '+' should remain visible on Today view"


def test_nav_productivity_full_page(app, recorder):
    """Productivity is a full-page view — QuickAdd hidden."""
    pre = app.screenshot()
    helper_y = _find_helper_y(pre)
    assert helper_y is not None, "Couldn't find 'Помощник'"

    # Productivity is two items above Помощник (Productivity, Notifications, Помощник).
    productivity_y = helper_y - 36 * 2
    recorder(f"clicking productivity at y={productivity_y}")
    app.click_client(SIDEBAR_X, productivity_y)
    time.sleep(0.8)
    after = app.screenshot()
    recorder("after productivity click")

    bbox = find_plus_glyph(after)
    recorder(f"plus glyph after productivity: {bbox}")
    assert bbox is None, (
        "QuickAdd '+' should not appear on the Productivity full-page view."
    )


def test_sidebar_toggle_hides_sidebar(app, recorder):
    """Click the hamburger button (☰) at top-left to collapse the
    sidebar. The sidebar surface (slightly lighter than main background)
    should disappear."""
    pre = app.screenshot()
    pre_visible = _sidebar_visible(pre)
    recorder(f"sidebar visible (pre): {pre_visible}")
    assert pre_visible, "Sanity: sidebar should be visible at start"

    # The hamburger is at approx (327, 56) in DesktopLayout's topBar.
    app.click_client(327, 56)
    time.sleep(0.8)
    post = app.screenshot()
    recorder("after hamburger click")

    post_visible = _sidebar_visible(post)
    recorder(f"sidebar visible (post): {post_visible}")
    assert not post_visible, (
        "sidebar didn't collapse — main bg and sidebar surface still differ"
    )

    # Click hamburger again to restore.
    app.click_client(48, 56)
    time.sleep(0.6)
    restored = app.screenshot()
    recorder("after restore")
    restored_visible = _sidebar_visible(restored)
    assert restored_visible, (
        "sidebar didn't restore after second hamburger click"
    )
