# Dev helper: capture the OpenTodo dev-build window and send Win32 clicks/drags.
# Usage:
#   _cap.ps1 -Out path.png                       # screenshot window
#   _cap.ps1 -ClickX 0.4 -ClickY 0.95 -Out p.png # click at fractional client coords, then shot
#   _cap.ps1 -DragFromX .. -DragFromY .. -DragToX .. -DragToY .. -Out p.png  # drag (swipe)
param(
  [string]$Out = (Join-Path $PSScriptRoot "_verify.png"),
  [double]$ClickX = -1, [double]$ClickY = -1,
  [double]$DragFromX = -1, [double]$DragFromY = -1, [double]$DragToX = -1, [double]$DragToY = -1
)
Add-Type -AssemblyName System.Drawing
$code = @"
using System; using System.Runtime.InteropServices;
public class CapW {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int n);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, IntPtr e);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
}
"@
try { Add-Type -TypeDefinition $code } catch {}
$LDOWN = 0x02; $LUP = 0x04
$p = Get-Process -Name OpenTodo -ErrorAction SilentlyContinue | Sort-Object StartTime -Descending | Select-Object -First 1
if (-not $p) { "no process"; exit 1 }
$h = $p.MainWindowHandle
# Force-raise above all windows + move to an empty corner (other apps may have
# topmost overlays at the original spot).
[void][CapW]::ShowWindow($h, 9)
[void][CapW]::SetWindowPos($h, [IntPtr](-1), 40, 40, 0, 0, 0x41)   # HWND_TOPMOST, move to 40,40, NOSIZE|SHOWWINDOW
[void][CapW]::SetForegroundWindow($h)
Start-Sleep -Milliseconds 500
# client-area origin + size in screen coords
$cr = New-Object CapW+RECT; [void][CapW]::GetClientRect($h, [ref]$cr)
$o = New-Object CapW+POINT; $o.X = 0; $o.Y = 0; [void][CapW]::ClientToScreen($h, [ref]$o)
$cw = $cr.Right - $cr.Left; $ch = $cr.Bottom - $cr.Top
function Pt([double]$fx,[double]$fy){ @([int]($o.X + $fx*$cw), [int]($o.Y + $fy*$ch)) }
if ($ClickX -ge 0) {
  $pt = Pt $ClickX $ClickY
  [void][CapW]::SetCursorPos($pt[0],$pt[1]); Start-Sleep -Milliseconds 120
  [CapW]::mouse_event($LDOWN,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 60
  [CapW]::mouse_event($LUP,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 600
}
if ($DragFromX -ge 0) {
  $a = Pt $DragFromX $DragFromY; $b = Pt $DragToX $DragToY
  [void][CapW]::SetCursorPos($a[0],$a[1]); Start-Sleep -Milliseconds 120
  [CapW]::mouse_event($LDOWN,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 80
  $steps = 24
  for ($i=1; $i -le $steps; $i++) {
    $x = [int]($a[0] + ($b[0]-$a[0])*$i/$steps); $y = [int]($a[1] + ($b[1]-$a[1])*$i/$steps)
    [void][CapW]::SetCursorPos($x,$y); Start-Sleep -Milliseconds 12
  }
  Start-Sleep -Milliseconds 80
  [CapW]::mouse_event($LUP,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 500
}
# capture full window rect
$wr = New-Object CapW+RECT; [void][CapW]::GetWindowRect($h, [ref]$wr)
$ww = $wr.Right - $wr.Left; $wh = $wr.Bottom - $wr.Top
$bmp = New-Object System.Drawing.Bitmap $ww, $wh
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($wr.Left, $wr.Top, 0, 0, (New-Object System.Drawing.Size($ww, $wh)))
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
"client ${cw}x${ch} @ $($o.X),$($o.Y) -> saved $Out"
