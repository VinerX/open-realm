param(
    [Parameter(Mandatory = $true)]
    [int]$ProcessId,

    [Parameter(Mandatory = $true)]
    [string]$OutputPath,

    [switch]$Activate
)

Add-Type -AssemblyName System.Drawing

if (-not ([System.Management.Automation.PSTypeName]'WindowCapture.NativeMethods').Type) {
    Add-Type @'
using System;
using System.Runtime.InteropServices;

namespace WindowCapture {
    [StructLayout(LayoutKind.Sequential)]
    public struct Rect {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    public static class NativeMethods {
        [DllImport("user32.dll", SetLastError = true)]
        public static extern bool GetWindowRect(IntPtr handle, out Rect rect);

        [DllImport("user32.dll", SetLastError = true)]
        public static extern bool PrintWindow(IntPtr handle, IntPtr hdc, uint flags);

        [DllImport("user32.dll", SetLastError = true)]
        public static extern uint GetWindowThreadProcessId(IntPtr handle, out uint processId);

        [DllImport("user32.dll")]
        public static extern bool ShowWindowAsync(IntPtr handle, int command);

        [DllImport("user32.dll")]
        public static extern bool SetForegroundWindow(IntPtr handle);

        [DllImport("user32.dll")]
        public static extern bool BringWindowToTop(IntPtr handle);

        [DllImport("user32.dll")]
        public static extern IntPtr GetForegroundWindow();

        [DllImport("kernel32.dll")]
        public static extern uint GetCurrentThreadId();

        [DllImport("user32.dll")]
        public static extern bool AttachThreadInput(uint attach, uint attachTo, bool attachState);

        [DllImport("user32.dll", SetLastError = true)]
        public static extern bool SetWindowPos(IntPtr handle, IntPtr insertAfter,
            int x, int y, int width, int height, uint flags);
    }
}
'@
}

$process = Get-Process -Id $ProcessId -ErrorAction Stop
$process.Refresh()
$handle = $process.MainWindowHandle
if ($handle -eq [IntPtr]::Zero) {
    throw "Process $ProcessId has no main window handle."
}

[uint32]$windowProcessId = 0
[void][WindowCapture.NativeMethods]::GetWindowThreadProcessId($handle, [ref]$windowProcessId)
if ($windowProcessId -ne $ProcessId) {
    throw "Main window handle belongs to process $windowProcessId, not $ProcessId."
}

if ($Activate) {
    $foreground = [WindowCapture.NativeMethods]::GetForegroundWindow()
    [uint32]$foregroundProcessId = 0
    $foregroundThread = [WindowCapture.NativeMethods]::GetWindowThreadProcessId($foreground, [ref]$foregroundProcessId)
    $currentThread = [WindowCapture.NativeMethods]::GetCurrentThreadId()
    $attached = $false
    if ($foregroundThread -and $foregroundThread -ne $currentThread) {
        $attached = [WindowCapture.NativeMethods]::AttachThreadInput($currentThread, $foregroundThread, $true)
    }
    try {
        [void][WindowCapture.NativeMethods]::ShowWindowAsync($handle, 9)
        [void][WindowCapture.NativeMethods]::SetWindowPos($handle, [IntPtr](-1), 0, 0, 0, 0, 0x43)
        [void][WindowCapture.NativeMethods]::SetWindowPos($handle, [IntPtr](-2), 0, 0, 0, 0, 0x43)
        [void][WindowCapture.NativeMethods]::BringWindowToTop($handle)
        [void][WindowCapture.NativeMethods]::SetForegroundWindow($handle)
    } finally {
        if ($attached) {
            [void][WindowCapture.NativeMethods]::AttachThreadInput($currentThread, $foregroundThread, $false)
        }
    }
    Start-Sleep -Milliseconds 800
}

$rect = New-Object WindowCapture.Rect
if (-not [WindowCapture.NativeMethods]::GetWindowRect($handle, [ref]$rect)) {
    throw "GetWindowRect failed for process $ProcessId."
}

$width = $rect.Right - $rect.Left
$height = $rect.Bottom - $rect.Top
if ($width -le 0 -or $height -le 0) {
    throw "Process $ProcessId returned an empty window rectangle."
}

$fullOutputPath = [System.IO.Path]::GetFullPath($OutputPath)
$outputDirectory = [System.IO.Path]::GetDirectoryName($fullOutputPath)
if ($outputDirectory -and -not (Test-Path -LiteralPath $outputDirectory)) {
    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
}

$bitmap = New-Object System.Drawing.Bitmap($width, $height)
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
try {
    $hdc = $graphics.GetHdc()
    try {
        $captured = [WindowCapture.NativeMethods]::PrintWindow($handle, $hdc, 2)
    } finally {
        $graphics.ReleaseHdc($hdc)
    }
    if (-not $captured) {
        $errorCode = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
        throw "PrintWindow failed for process $ProcessId (Win32 error $errorCode)."
    }
    $bitmap.Save($fullOutputPath, [System.Drawing.Imaging.ImageFormat]::Png)
} finally {
    $graphics.Dispose()
    $bitmap.Dispose()
}

Write-Output $fullOutputPath
