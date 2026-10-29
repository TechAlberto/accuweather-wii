# Build helper for Windows.
# devkitPro's make dislikes spaces in paths, so if the project lives under a
# path with spaces we temporarily map it to a free drive letter.

param(
    [switch]$Clean,
    [switch]$Libs,
    [switch]$Autocycle,
    [string[]]$Extra
)

$ErrorActionPreference = "Stop"

$root = (Resolve-Path $PSScriptRoot).Path
$env:DEVKITPRO = "C:/devkitPro"
$env:DEVKITPPC = "C:/devkitPro/devkitPPC"
$env:PATH = "C:\devkitPro\devkitPPC\bin;C:\devkitPro\tools\bin;" + $env:PATH
$make = "C:\devkitPro\msys2\usr\bin\make.exe"
$bash = "C:\devkitPro\msys2\usr\bin\bash.exe"

if (-not (Test-Path $make)) {
    throw "devkitPro not found at C:\devkitPro (install it from devkitpro.org)"
}

$buildArgs = @()
if ($Autocycle) { $buildArgs += "UI_AUTOCYCLE=1" }
if ($Extra) {
    # `powershell -File` passes "A=1,B=1" as one string, so split it apart;
    # otherwise make sees a single VAR=value and silently drops the rest.
    foreach ($item in $Extra) {
        foreach ($part in ($item -split '[,;]')) {
            $p = $part.Trim()
            if ($p) { $buildArgs += $p }
        }
    }
}

# mbedTLS and libwiiupdate are prebuilt by tools/build_libs.sh; build them on
# demand so a fresh checkout only needs build.ps1.
$needLibs = $Libs -or
            -not (Test-Path (Join-Path $root "libs\ppc\lib\libmbedtls.a")) -or
            -not (Test-Path (Join-Path $root "libs\ppc\lib\libwiiupdate.a"))

$mapped = $null
$workDir = $root
if ($root -match ' ') {
    foreach ($d in @("V", "W", "X", "Y", "Z")) {
        if (-not (Test-Path "${d}:\")) {
            & subst "${d}:" $root
            if ($LASTEXITCODE -eq 0) { $mapped = "${d}:"; break }
        }
    }
    if ($mapped) { $workDir = "${mapped}\" }
}

# MSYS2 path for bash -lc.
if ($mapped) {
    $unixDir = "/" + $mapped.Substring(0, 1).ToLower()
} else {
    $unixDir = ($root -replace '\\', '/') -replace '^([A-Za-z]):', '/$1'
    $unixDir = $unixDir.ToLower() -replace '^/([a-z]):', '/$1'
}

Push-Location $workDir
$code = 0
try {
    if ($needLibs -and $code -eq 0) {
        Write-Host "Building mbedTLS and libwiiupdate into libs/ppc ..." -ForegroundColor Cyan
        & $bash -lc "cd '$unixDir' && DEVKITPRO=/c/devkitPro bash tools/build_libs.sh"
        $code = $LASTEXITCODE
    }
    if ($Clean -and $code -eq 0) {
        # Two invocations: "make clean VAR=1" treats the assignments as
        # variables and only runs the clean goal, so nothing gets rebuilt.
        & $make clean
        $code = $LASTEXITCODE
    }
    if ($code -eq 0) {
        & $make @buildArgs
        $code = $LASTEXITCODE
    }
} finally {
    Pop-Location
    if ($mapped) { & subst "${mapped}" /D | Out-Null }
}

if ($code -ne 0) { exit $code }

$dol = Join-Path $root "accuweather-wii.dol"
if (Test-Path $dol) {
    Write-Host ""
    Write-Host "Built $dol ($([math]::Round((Get-Item $dol).Length / 1KB)) KB)" -ForegroundColor Green
}
exit 0
