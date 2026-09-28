<#
.SYNOPSIS
Capture a Windows application window to a PNG file.

.EXAMPLE
.\tools\capture_window.ps1 -ProcessId 1234 -DelaySeconds 20 -Output build\captures\game.png

.EXAMPLE
.\tools\capture_window.ps1 -TitlePattern 'OpenWarcraft3' -Output build\captures\game.png
#>
[CmdletBinding(DefaultParameterSetName = 'Process')]
param(
    [Parameter(Mandatory = $true, ParameterSetName = 'Process')]
    [int]$ProcessId,

    [Parameter(Mandatory = $true, ParameterSetName = 'Title')]
    [string]$TitlePattern,

    [Parameter(Mandatory = $true, ParameterSetName = 'List')]
    [switch]$ListWindows,

    [string]$Output = 'build\captures\window.png',

    [ValidateRange(0, 120)]
    [int]$DelaySeconds = 15,

    [ValidateSet('Auto', 'PrintWindow', 'Screen')]
    [string]$Method = 'Auto'
)

$ErrorActionPreference = 'Stop'

if (-not ('WindowCaptureNative' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class WindowCaptureNative {
    [StructLayout(LayoutKind.Sequential)] public struct RECT {
        public int Left, Top, Right, Bottom;
    }
    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);
    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr hwnd);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hwnd, int command);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hwnd);
    public delegate bool EnumWindowsProc(IntPtr hwnd, IntPtr lParam);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumWindowsProc callback, IntPtr lParam);
    [DllImport("user32.dll", SetLastError = true)] static extern IntPtr OpenInputDesktop(uint flags, bool inherit, uint access);
    [DllImport("user32.dll")] static extern bool EnumDesktopWindows(IntPtr desktop, EnumWindowsProc callback, IntPtr lParam);
    [DllImport("user32.dll")] static extern bool CloseDesktop(IntPtr desktop);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint processId);
    [DllImport("user32.dll")] static extern int GetWindowTextLength(IntPtr hwnd);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    static extern int GetWindowText(IntPtr hwnd, System.Text.StringBuilder text, int maxCount);
    public static IntPtr[] GetProcessWindows(int targetProcessId) {
        List<IntPtr> windows = new List<IntPtr>();
        EnumWindowsProc callback = delegate(IntPtr hwnd, IntPtr ignored) {
            uint processId;
            GetWindowThreadProcessId(hwnd, out processId);
            if (processId == (uint)targetProcessId && IsWindowVisible(hwnd)) windows.Add(hwnd);
            return true;
        };
        IntPtr desktop = OpenInputDesktop(0, false, 0x0001);
        if (desktop != IntPtr.Zero) {
            try { EnumDesktopWindows(desktop, callback, IntPtr.Zero); }
            finally { CloseDesktop(desktop); }
        } else {
            EnumWindows(callback, IntPtr.Zero);
        }
        return windows.ToArray();
    }
    public static IntPtr[] GetVisibleWindows() {
        List<IntPtr> windows = new List<IntPtr>();
        EnumWindowsProc callback = delegate(IntPtr hwnd, IntPtr ignored) {
            if (IsWindowVisible(hwnd)) windows.Add(hwnd);
            return true;
        };
        IntPtr desktop = OpenInputDesktop(0, false, 0x0001);
        if (desktop != IntPtr.Zero) {
            try { EnumDesktopWindows(desktop, callback, IntPtr.Zero); }
            finally { CloseDesktop(desktop); }
        } else {
            EnumWindows(callback, IntPtr.Zero);
        }
        return windows.ToArray();
    }
    public static string GetTitle(IntPtr hwnd) {
        int length = GetWindowTextLength(hwnd);
        System.Text.StringBuilder text = new System.Text.StringBuilder(length + 1);
        GetWindowText(hwnd, text, text.Capacity);
        return text.ToString();
    }
    public static int GetWindowProcessId(IntPtr hwnd) {
        uint processId;
        GetWindowThreadProcessId(hwnd, out processId);
        return (int)processId;
    }
    public static long GetArea(IntPtr hwnd) {
        RECT rect;
        if (!GetWindowRect(hwnd, out rect)) return 0;
        return (long)(rect.Right - rect.Left) * (rect.Bottom - rect.Top);
    }
}
'@
}

if ($ListWindows) {
    $processById = @{}
    Get-Process | ForEach-Object { $processById[$_.Id] = $_ }
    [WindowCaptureNative]::GetVisibleWindows() | ForEach-Object {
        $handle = $_
        $owner = [WindowCaptureNative]::GetWindowProcessId($handle)
        if ($processById.ContainsKey($owner)) {
            $process = $processById[$owner]
            [pscustomobject]@{
                ProcessId = $process.Id
                ProcessName = $process.ProcessName
                Title = [WindowCaptureNative]::GetTitle($handle)
                Area = [WindowCaptureNative]::GetArea($handle)
                Handle = $handle
            }
        }
    } | Sort-Object Area -Descending | Format-Table -AutoSize
    exit 0
}

if ($PSCmdlet.ParameterSetName -eq 'Process') {
    $target = Get-Process -Id $ProcessId -ErrorAction Stop
    $candidates = @([WindowCaptureNative]::GetProcessWindows($target.Id) | ForEach-Object {
        [pscustomobject]@{ Handle = $_; Title = [WindowCaptureNative]::GetTitle($_); Area = [WindowCaptureNative]::GetArea($_) }
    })
    if ($candidates.Count -eq 0 -and $target.MainWindowHandle -ne [IntPtr]::Zero) {
        $handle = [IntPtr]$target.MainWindowHandle
        $candidates = @([pscustomobject]@{
            Handle = $handle
            Title = [WindowCaptureNative]::GetTitle($handle)
            Area = [WindowCaptureNative]::GetArea($handle)
        })
    }
} else {
    $candidates = @(Get-Process | ForEach-Object {
        $process = $_
        [WindowCaptureNative]::GetProcessWindows($process.Id) | ForEach-Object {
            [pscustomobject]@{ Handle = $_; Title = [WindowCaptureNative]::GetTitle($_); Area = [WindowCaptureNative]::GetArea($_); Process = $process }
        }
    } | Where-Object { $_.Title -match $TitlePattern })
    $matches = $candidates
    if ($matches.Count -ne 1) {
        $choices = $matches | ForEach-Object { "PID $($_.Process.Id): $($_.Title)" }
        throw "TitlePattern must match exactly one window; matches: $($choices -join '; ')"
    }
    $target = $matches[0].Process
}

$candidates = @($candidates | Sort-Object Area -Descending)
if ($candidates.Count -eq 0) {
    throw "Process $($target.Id) has no top-level window."
}
$hwnd = [IntPtr]$candidates[0].Handle
$windowTitle = $candidates[0].Title
if ($DelaySeconds -gt 0) { Start-Sleep -Seconds $DelaySeconds }

$rect = New-Object WindowCaptureNative+RECT
if (-not [WindowCaptureNative]::GetWindowRect($hwnd, [ref]$rect)) {
    throw "GetWindowRect failed for PID $($target.Id)."
}
$width = $rect.Right - $rect.Left
$height = $rect.Bottom - $rect.Top
if ($width -le 0 -or $height -le 0) {
    throw "Window has invalid bounds: ${width}x${height}."
}

$bitmap = New-Object System.Drawing.Bitmap($width, $height, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$captureMethod = $Method
try {
    if ($Method -ne 'Screen') {
        $hdc = $graphics.GetHdc()
        try {
            $captured = [WindowCaptureNative]::PrintWindow($hwnd, $hdc, 2)
            if (-not $captured -and $Method -eq 'PrintWindow') {
                throw "PrintWindow failed for PID $($target.Id)."
            }
            if ($captured) {
                $captureMethod = 'PrintWindow'
            } elseif ($Method -eq 'Auto') {
                $captureMethod = 'Screen'
            }
        } finally {
            $graphics.ReleaseHdc($hdc)
        }

        if ($captureMethod -eq 'PrintWindow') {
            $brightSamples = 0
            $colorSamples = 0
            for ($y = 0; $y -lt $height; $y += [Math]::Max(1, [int]($height / 48))) {
                for ($x = 0; $x -lt $width; $x += [Math]::Max(1, [int]($width / 48))) {
                    $pixel = $bitmap.GetPixel($x, $y)
                    if ($pixel.R -gt 28 -or $pixel.G -gt 28 -or $pixel.B -gt 28) { $brightSamples++ }
                    if ($pixel.R -ne $pixel.G -or $pixel.G -ne $pixel.B) { $colorSamples++ }
                }
            }
            if ($brightSamples -lt 12 -and $colorSamples -lt 12) {
                if ($Method -eq 'PrintWindow') {
                    throw 'PrintWindow returned an almost-black frame; use -Method Screen.'
                }
                $captureMethod = 'Screen'
            }
        }
    }

    if ($captureMethod -eq 'Screen') {
        if ([WindowCaptureNative]::IsIconic($hwnd)) {
            [void][WindowCaptureNative]::ShowWindow($hwnd, 9)
            Start-Sleep -Milliseconds 250
            [void][WindowCaptureNative]::GetWindowRect($hwnd, [ref]$rect)
            $width = $rect.Right - $rect.Left
            $height = $rect.Bottom - $rect.Top
            $graphics.Dispose()
            $bitmap.Dispose()
            $bitmap = New-Object System.Drawing.Bitmap($width, $height, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
            $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        }
        [void][WindowCaptureNative]::SetForegroundWindow($hwnd)
        Start-Sleep -Milliseconds 250
        [void][WindowCaptureNative]::GetWindowRect($hwnd, [ref]$rect)
        $newWidth = $rect.Right - $rect.Left
        $newHeight = $rect.Bottom - $rect.Top
        if ($newWidth -le 0 -or $newHeight -le 0) {
            throw "Window has invalid bounds after restore: ${newWidth}x${newHeight}."
        }
        if ($newWidth -ne $width -or $newHeight -ne $height) {
            $width = $newWidth
            $height = $newHeight
            $graphics.Dispose()
            $bitmap.Dispose()
            $bitmap = New-Object System.Drawing.Bitmap($width, $height, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
            $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        }
        $graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0,
            (New-Object System.Drawing.Size($width, $height)),
            [System.Drawing.CopyPixelOperation]::SourceCopy)
    }

    $fullOutput = [System.IO.Path]::GetFullPath($Output)
    $directory = [System.IO.Path]::GetDirectoryName($fullOutput)
    if (-not [System.IO.Directory]::Exists($directory)) {
        [void][System.IO.Directory]::CreateDirectory($directory)
    }
    $bitmap.Save($fullOutput, [System.Drawing.Imaging.ImageFormat]::Png)
    [pscustomobject]@{
        ProcessId = $target.Id
        Title = $windowTitle
        Method = $captureMethod
        DelaySeconds = $DelaySeconds
        CapturedAt = Get-Date -Format o
        Width = $width
        Height = $height
        Path = $fullOutput
    } | Format-List
} finally {
    $graphics.Dispose()
    $bitmap.Dispose()
}
