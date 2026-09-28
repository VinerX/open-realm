<#
.SYNOPSIS
Launches a Warcraft III map in a visible OpenWarcraft3 window.

.EXAMPLE
.\tools\run_wc3_map.ps1 -Map 'C:\Maps\Test.w3x' -WarcraftData 'E:\Games\Warcraft III'
#>
param(
    [Parameter(Mandatory = $true)]
    [string]$Map,

    [Parameter(Mandatory = $true)]
    [string]$WarcraftData,

    [ValidateSet('TFT', 'RoC')]
    [string]$Edition = 'TFT',

    [string]$RuntimeDirectory = ''
)

$ErrorActionPreference = 'Stop'

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$buildDir = Join-Path $repoRoot 'build'
$binary = Join-Path $buildDir 'bin\openwarcraft3.exe'
$mapPath = [System.IO.Path]::GetFullPath($Map)
$dataPath = [System.IO.Path]::GetFullPath($WarcraftData)

if (-not (Test-Path -LiteralPath $mapPath -PathType Leaf)) {
    throw "Map file not found: $mapPath"
}
if ([System.IO.Path]::GetExtension($mapPath) -notin @('.w3m', '.w3x')) {
    throw "Expected a .w3m or .w3x map: $mapPath"
}
if (-not (Test-Path -LiteralPath $dataPath -PathType Container)) {
    throw "Warcraft III data directory not found: $dataPath"
}
if (-not (Test-Path -LiteralPath $binary -PathType Leaf)) {
    throw "OpenWarcraft3 binary not found: $binary (build it first)"
}

$gameDlls = @('libgame.dll', 'libjass.dll', 'libmenu.dll', 'librenderer.dll', 'libshared.dll', 'libsheet.dll')
$missingGameDlls = @($gameDlls | Where-Object {
    -not (Test-Path -LiteralPath (Join-Path $buildDir "lib\$_") -PathType Leaf)
})
if ($missingGameDlls.Count -gt 0) {
    throw "Missing engine DLLs in build\lib: $($missingGameDlls -join ', ')"
}

$runtimeCandidates = @()
if ($RuntimeDirectory) {
    $runtimeCandidates += [System.IO.Path]::GetFullPath($RuntimeDirectory)
}
$runtimeCandidates += @(
    (Join-Path $env:TEMP 'wc3lua\msys2out\msys64\ucrt64\bin'),
    (Join-Path $env:ProgramFiles 'Git\mingw64\bin'),
    (Join-Path $buildDir 'bin'),
    (Join-Path $buildDir 'lib')
)
$runtimeDirectories = @($runtimeCandidates | Where-Object {
    $_ -and (Test-Path -LiteralPath $_ -PathType Container)
} | Select-Object -Unique)
$requiredRuntimeDlls = @('SDL2.dll', 'libepoxy-0.dll', 'zlib1.dll', 'libstdc++-6.dll', 'libgcc_s_seh-1.dll', 'libwinpthread-1.dll')
$missingRuntimeDlls = @($requiredRuntimeDlls | Where-Object {
    $name = $_
    -not ($runtimeDirectories | Where-Object { Test-Path -LiteralPath (Join-Path $_ $name) -PathType Leaf })
})
if ($missingRuntimeDlls.Count -gt 0) {
    throw "Missing runtime DLLs: $($missingRuntimeDlls -join ', '). Set -RuntimeDirectory to the MSYS2 UCRT64 bin directory."
}

$childPath = (@($runtimeDirectories) + (Join-Path $buildDir 'bin') + (Join-Path $buildDir 'lib') + $env:PATH) -join ';'
$start = [System.Diagnostics.ProcessStartInfo]::new()
$start.FileName = $binary
$start.WorkingDirectory = Join-Path $buildDir 'bin'
$start.UseShellExecute = $false
$start.CreateNoWindow = $false
$start.WindowStyle = [System.Diagnostics.ProcessWindowStyle]::Normal
$start.Environment['PATH'] = $childPath

foreach ($argument in @('-data', $dataPath)) {
    $start.ArgumentList.Add($argument)
}
if ($Edition -eq 'TFT') {
    $start.ArgumentList.Add('-tft')
} else {
    $start.ArgumentList.Add('-roc')
}
$start.ArgumentList.Add('+set')
$start.ArgumentList.Add('extra_data')
$start.ArgumentList.Add([System.IO.Path]::GetDirectoryName($mapPath))
$start.ArgumentList.Add('+set')
$start.ArgumentList.Add('vid_hidden')
$start.ArgumentList.Add('0')
$start.ArgumentList.Add('+map')
$start.ArgumentList.Add([System.IO.Path]::GetFileName($mapPath))

$process = [System.Diagnostics.Process]::Start($start)
Start-Sleep -Seconds 2
$process.Refresh()
if ($process.HasExited) {
    $exitCode = [BitConverter]::ToUInt32([BitConverter]::GetBytes([int]$process.ExitCode), 0)
    throw ('OpenWarcraft3 exited during startup (code 0x{0:X8}). Check its startup dialog.' -f $exitCode)
}

Write-Host "Opened visible Warcraft III $Edition map: $mapPath (PID $($process.Id))"
