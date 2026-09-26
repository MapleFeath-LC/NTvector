# Build Program.exe (Release, x64) for OPEN_NTVECTOR_NEW.
#
# Every build refreshes the dist\ folder so it is always a complete, directly
# runnable output directory:
#
#   dist\Program.exe          <- the freshly compiled main binary
#   dist\python312.dll        <- runtime (copied in if missing)
#   dist\python3.dll
#   dist\vcruntime140.dll
#   dist\vanilla.mcp          <- MCP script bundle
#   dist\skin_data.json
#   dist\python312\           <- stdlib + site-packages
#
# Usage:
#   .\build_release.ps1                 # build + refresh dist
#   .\build_release.ps1 -RefreshRuntime # also re-copy the runtime files
#
# Notes:
# * The CMake build tree stays in .\build\ (intermediate files are hundreds of
#   MB); only the finished artifacts are placed in dist\.
# * This machine's environment contains case-duplicate proxy variables
#   (NO_PROXY + no_proxy, HTTPS_PROXY + https_proxy, ...). .NET's
#   ProcessStartInfo.EnvironmentVariables is a case-insensitive dictionary, so
#   MSBuild throws "MSB6001 ... duplicate dictionary key" when it launches
#   CL.exe. The duplicates are removed inside this build process only; the
#   machine environment is left untouched.

param(
    [switch]$RefreshRuntime
)

foreach ($n in @('no_proxy','NO_PROXY','http_proxy','HTTP_PROXY',
                 'https_proxy','HTTPS_PROXY','all_proxy','ALL_PROXY')) {
    try { [System.Environment]::SetEnvironmentVariable($n, $null) } catch { }
}

$ErrorActionPreference = 'Continue'
$root  = $PSScriptRoot
$build = Join-Path $root 'build'
$dist  = Join-Path $root 'dist'
$ref   = 'D:\Program\Vector\VQ\pubfd'   # reference deployment for runtime files

Write-Host '== configure =='
cmake -S $root -B $build -G "Visual Studio 18 2026" -A x64 -DCMAKE_BUILD_TYPE=Release | Out-Null
if ($LASTEXITCODE -ne 0) { Write-Host 'configure FAILED'; exit 1 }

Write-Host '== build (Release) =='
cmake --build $build --config Release --parallel
if ($LASTEXITCODE -ne 0) { Write-Host 'build FAILED'; exit 1 }

$exe = Join-Path $build 'application\Release\Program.exe'
if (-not (Test-Path $exe)) { Write-Host 'Program.exe not produced'; exit 1 }

# --- refresh dist -----------------------------------------------------------
New-Item -ItemType Directory -Force -Path $dist | Out-Null
Copy-Item $exe (Join-Path $dist 'Program.exe') -Force

$runtimeFiles = @('python312.dll', 'python3.dll', 'vcruntime140.dll', 'vanilla.mcp', 'skin_data.json')
$copied = @()
foreach ($f in $runtimeFiles) {
    $target = Join-Path $dist $f
    if ($RefreshRuntime -or -not (Test-Path $target)) {
        $source = Join-Path $ref $f
        if (Test-Path $source) {
            Copy-Item $source $target -Force
            $copied += $f
        } else {
            Write-Host "  WARNING: runtime file not found: $source"
        }
    }
}

$pyDir = Join-Path $dist 'python312'
if ($RefreshRuntime -or -not (Test-Path $pyDir)) {
    $pySrc = Join-Path $ref 'python312'
    if (Test-Path $pySrc) {
        Copy-Item $pySrc $dist -Recurse -Force
        $copied += 'python312\'
    } else {
        Write-Host "  WARNING: python312 runtime dir not found: $pySrc"
    }
}

# --- report -----------------------------------------------------------------
Write-Host ''
Write-Host "OK  built $([math]::Round((Get-Item $exe).Length / 1MB, 2)) MB -> $dist\Program.exe"
if ($copied.Count -gt 0) { Write-Host "    runtime refreshed: $($copied -join ', ')" }

$missing = @()
foreach ($f in $runtimeFiles) { if (-not (Test-Path (Join-Path $dist $f))) { $missing += $f } }
if (-not (Test-Path (Join-Path $dist 'python312'))) { $missing += 'python312\' }
if ($missing.Count -gt 0) {
    Write-Host "    MISSING from dist: $($missing -join ', ')"
    Write-Host "    (set -RefreshRuntime, or check the reference path: $ref)"
} else {
    Write-Host '    dist is complete (exe + runtime)'
}

Write-Host ''
Write-Host 'Run it:'
Write-Host "    cd /d `"$dist`" && Program.exe            (normal start)"
Write-Host "    cd /d `"$dist`" && Program.exe --ui-demo  (preview the 3 UI states)"
