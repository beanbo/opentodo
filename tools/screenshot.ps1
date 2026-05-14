#requires -Version 5.0
<#
    OpenTodo screenshot helper.

    Captures the main window of OpenTodo (or any process by name) into a PNG
    file. Used for visual regression checks during development —
    the assistant reads the resulting PNG via its multimodal capability.

    Usage:
        pwsh tools/screenshot.ps1                      # default: OpenTodo → opentodo.png
        pwsh tools/screenshot.ps1 -Name "OpenTodo" -Out screenshots/inbox.png
        pwsh tools/screenshot.ps1 -Pid 12345 -Out shot.png
#>

[CmdletBinding()]
param(
    [string] $Name = 'OpenTodo',
    [int]    $ProcessId  = 0,
    [string] $Out  = 'opentodo.png'
)

Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies @('System.Drawing') -TypeDefinition @"
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

public static class WinShot {
    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT rc);
    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")]
    public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    [DllImport("user32.dll")]
    public static extern bool IsIconic(IntPtr hWnd);

    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }

    public static void CaptureRect(IntPtr hWnd, string path) {
        if (IsIconic(hWnd)) { ShowWindow(hWnd, 9); }   // SW_RESTORE
        ShowWindow(hWnd, 5);                           // SW_SHOW
        SetForegroundWindow(hWnd);
        System.Threading.Thread.Sleep(250);

        RECT rc;
        if (!GetWindowRect(hWnd, out rc))
            throw new Exception("GetWindowRect failed");

        int w = rc.Right - rc.Left;
        int h = rc.Bottom - rc.Top;
        if (w <= 0 || h <= 0)
            throw new Exception("Empty window rect");

        using (Bitmap bmp = new Bitmap(w, h, PixelFormat.Format32bppArgb))
        using (Graphics g = Graphics.FromImage(bmp)) {
            g.CopyFromScreen(rc.Left, rc.Top, 0, 0, new Size(w, h));
            bmp.Save(path, ImageFormat.Png);
        }
    }
}
"@ -Language CSharp

# Pick the target process.
$proc = $null
if ($ProcessId -gt 0) {
    $proc = Get-Process -Id $ProcessId -ErrorAction Stop
} else {
    $proc = Get-Process -Name $Name -ErrorAction SilentlyContinue |
            Where-Object { $_.MainWindowHandle -ne [IntPtr]::Zero } |
            Select-Object -First 1
}

if (-not $proc) {
    Write-Error "No window found for $(if ($ProcessId) { "PID $ProcessId" } else { "process '$Name'" })"
    exit 1
}

$absOut = [System.IO.Path]::GetFullPath($Out)
$dir    = [System.IO.Path]::GetDirectoryName($absOut)
if ($dir -and -not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }

[WinShot]::CaptureRect($proc.MainWindowHandle, $absOut)
Write-Output "saved $absOut ($([math]::Round((Get-Item $absOut).Length / 1KB)) KB)"
