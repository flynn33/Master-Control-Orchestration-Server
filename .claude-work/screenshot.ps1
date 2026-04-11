# Screenshot helper for the autonomous build session.
# Launches a target exe, waits N seconds, then captures the foreground window
# (or the full primary screen if no window handle found) and saves as PNG.

param(
  [Parameter(Mandatory = $true)] [string] $Exe,
  [Parameter(Mandatory = $true)] [string] $OutPng,
  [int] $DelaySeconds = 6,
  [string] $WindowTitleHint = "Master Control",
  [int] $ScrollDown = 0
)

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class WinApi {
  [DllImport("user32.dll")]
  public static extern IntPtr FindWindow(string lpClassName, string lpWindowName);
  [DllImport("user32.dll")]
  public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);
  [DllImport("user32.dll")]
  public static extern bool SetForegroundWindow(IntPtr hWnd);
  [DllImport("user32.dll")]
  public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)]
  public static extern IntPtr FindWindowEx(IntPtr parent, IntPtr child, string className, string window);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)]
  public static extern int GetWindowText(IntPtr hWnd, System.Text.StringBuilder lpString, int nMaxCount);
  [DllImport("user32.dll")]
  public static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);
  [DllImport("user32.dll")]
  public static extern bool IsWindowVisible(IntPtr hWnd);
  [DllImport("user32.dll")]
  public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdcBlt, uint nFlags);
  [DllImport("user32.dll")]
  public static extern bool SetWindowPos(IntPtr hWnd, IntPtr hWndInsertAfter, int X, int Y, int cx, int cy, uint uFlags);
  [DllImport("gdi32.dll")]
  public static extern bool BitBlt(IntPtr hObject, int nXDest, int nYDest, int nWidth, int nHeight, IntPtr hObjectSource, int nXSrc, int nYSrc, int dwRop);
  public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
}
public struct RECT { public int Left, Top, Right, Bottom; }
"@

Write-Host "Launching: $Exe"
$proc = Start-Process -FilePath $Exe -PassThru
Write-Host "PID: $($proc.Id)"

Start-Sleep -Seconds $DelaySeconds

# Find a visible window whose title contains $WindowTitleHint
$foundHandle = [IntPtr]::Zero
$foundTitle = ""
$script:matched = $null

$cb = [WinApi+EnumWindowsProc] {
  param($hWnd, $lParam)
  if (-not [WinApi]::IsWindowVisible($hWnd)) { return $true }
  $sb = New-Object System.Text.StringBuilder 512
  $len = [WinApi]::GetWindowText($hWnd, $sb, 512)
  if ($len -gt 0) {
    $title = $sb.ToString()
    if ($title -like "*$WindowTitleHint*") {
      $script:matched = @{ Handle = $hWnd; Title = $title }
      return $false
    }
  }
  return $true
}
[WinApi]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null

if ($null -ne $script:matched) {
  $foundHandle = $script:matched.Handle
  $foundTitle = $script:matched.Title
  Write-Host "Target window found: '$foundTitle'"
  # Resize to a larger canvas so dense Tron sections fit — the reference
  # image is ~1920x1080, and Server 2022 RDP defaults to 1280x720 which
  # clips the sub-agent pill row. 1 = SWP_NOZORDER omitted so it also raises.
  [WinApi]::SetWindowPos($foundHandle, [IntPtr]::Zero, 40, 40, 1800, 1000, 0x0040) | Out-Null
  [WinApi]::SetForegroundWindow($foundHandle) | Out-Null
  Start-Sleep -Milliseconds 2000

  if ($ScrollDown -gt 0) {
    Write-Host "Sending $ScrollDown PageDown keystrokes to bring sub-agent footer into view."
    Add-Type -AssemblyName System.Windows.Forms
    for ($i = 0; $i -lt $ScrollDown; $i++) {
      [System.Windows.Forms.SendKeys]::SendWait("{END}")
      Start-Sleep -Milliseconds 250
    }
    Start-Sleep -Milliseconds 500
  }

  # Capture the target window's contents via PrintWindow — renders the window
  # directly into our DC regardless of z-order. CopyFromScreen would grab
  # whatever overlaps the window rect in the current RDP session.
  $rect = New-Object RECT
  [WinApi]::GetWindowRect($foundHandle, [ref]$rect) | Out-Null
  $w = [Math]::Max(1, $rect.Right - $rect.Left)
  $h = [Math]::Max(1, $rect.Bottom - $rect.Top)
  Write-Host "Window rect: ($($rect.Left),$($rect.Top)) $w x $h"
  $bmp = New-Object System.Drawing.Bitmap $w, $h
  $gfx = [System.Drawing.Graphics]::FromImage($bmp)
  $hdc = $gfx.GetHdc()
  # PW_RENDERFULLCONTENT = 0x00000002 (required for DirectComposition-based WinUI 3 windows)
  $ok = [WinApi]::PrintWindow($foundHandle, $hdc, 2)
  Write-Host "PrintWindow(PW_RENDERFULLCONTENT) returned: $ok"
  $gfx.ReleaseHdc($hdc)
  if (-not $ok) {
    # Fallback to CopyFromScreen if PrintWindow declined
    $gfx2 = [System.Drawing.Graphics]::FromImage($bmp)
    $gfx2.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bmp.Size)
    $gfx2.Dispose()
  }
  $bmp.Save($OutPng, [System.Drawing.Imaging.ImageFormat]::Png)
  $gfx.Dispose()
  $bmp.Dispose()
} else {
  Write-Host "No matching window found; capturing primary screen instead."
  $screen = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
  $bmp = New-Object System.Drawing.Bitmap $screen.Width, $screen.Height
  $gfx = [System.Drawing.Graphics]::FromImage($bmp)
  $gfx.CopyFromScreen($screen.Left, $screen.Top, 0, 0, $bmp.Size)
  $bmp.Save($OutPng, [System.Drawing.Imaging.ImageFormat]::Png)
  $gfx.Dispose()
  $bmp.Dispose()
}

Write-Host "Saved: $OutPng"

# Clean up the launched process
try {
  Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
} catch {}
Start-Sleep -Seconds 1
# Also kill any other stragglers by name
Get-Process -Name "MasterControlShell" -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
