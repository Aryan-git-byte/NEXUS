param(
    [string]$IdfPath = $env:IDF_PATH,
    [string]$ToolsPath = $env:IDF_TOOLS_PATH,
    [ValidateSet('build','reconfigure','size','flash','monitor')][string]$Command = 'build',
    [string]$Port = '',
    [ValidateRange(1,32)][int]$Jobs = 4,
    [switch]$DisableCompilerCache,
    [switch]$UseUniversalCompiler,
    [ValidatePattern('^[a-z][a-z0-9_-]{0,31}$')][string]$BuildDirectory = 'build'
)
$ErrorActionPreference = 'Stop'
if ($Command -eq 'flash' -and -not $Port) { throw 'Pass -Port with the ESP32-S3 download-mode COM port.' }
$nexusRoot = Split-Path -Parent $PSScriptRoot
$nexusBuildPath = Join-Path $nexusRoot "firmware\$BuildDirectory"
$nexusCmakeCache = Join-Path $nexusBuildPath 'CMakeCache.txt'
if (-not $UseUniversalCompiler -and (Test-Path -LiteralPath $nexusCmakeCache)) {
    # CMake remembers the selected toolchain. Reuse its required DLL search
    # path on later invocations even when the original switch was omitted.
    $UseUniversalCompiler = [bool](Select-String -LiteralPath $nexusCmakeCache -Quiet -Pattern '^NEXUS_UNIVERSAL_XTENSA:[^=]+=(ON|YES|TRUE|1)$')
}
if (-not $IdfPath) {
    $nexusIdf = Get-ChildItem -LiteralPath 'C:\Espressif\frameworks' -Directory -ErrorAction SilentlyContinue |
        Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'export.ps1') } |
        Sort-Object Name -Descending | Select-Object -First 1
    if ($nexusIdf) { $IdfPath = $nexusIdf.FullName }
}
if (-not $IdfPath -or -not (Test-Path -LiteralPath (Join-Path $IdfPath 'export.ps1'))) {
    throw 'Install ESP-IDF 5.5.x, then pass -IdfPath or run this script from an ESP-IDF terminal.'
}
if (-not $ToolsPath -and $IdfPath.StartsWith('C:\Espressif\')) { $ToolsPath = 'C:\Espressif' }
if ($ToolsPath) { $env:IDF_TOOLS_PATH = $ToolsPath }
$env:IDF_PATH = $IdfPath
$env:IDF_COMPONENT_CACHE_PATH = Join-Path $nexusRoot '.cache\idf-components'
$env:PYTHONDONTWRITEBYTECODE = '1'
# Keep Ninja's compiler command lines below Windows process-launch limits.
$env:CMAKE_NINJA_FORCE_RESPONSE_FILE = '1'
. (Join-Path $IdfPath 'export.ps1')
if ($UseUniversalCompiler) {
    $nexusCompilerPath = (Get-Command xtensa-esp-elf-gcc.exe -ErrorAction Stop).Source
    $nexusCompilerLibrary = [IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $nexusCompilerPath) '..\lib'))
    $env:PATH = "$nexusCompilerLibrary;$env:PATH"
}
$nexusArgs = @('-C', (Join-Path $nexusRoot 'firmware'), '-D', 'IDF_TARGET=esp32s3')
$nexusArgs += @('-B', $nexusBuildPath)
if ($UseUniversalCompiler) {
    $nexusArgs += @('-D', 'NEXUS_UNIVERSAL_XTENSA=ON')
}
if ($DisableCompilerCache) { $nexusArgs += '--no-ccache' }
if ($Port) { $nexusArgs += @('-p', $Port) }
$nexusArgs += $(if ($Command -in @('build','flash')) { 'reconfigure' } else { $Command })
New-Item -ItemType Directory -Force -Path (Join-Path $nexusRoot 'test-results') | Out-Null
$nexusPreviousPreference = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
& python (Join-Path $IdfPath 'tools\idf.py') @nexusArgs 2>&1 |
    Tee-Object -FilePath (Join-Path $nexusRoot "test-results\firmware-$Command.log")
$nexusExitCode = $LASTEXITCODE
if ($nexusExitCode -eq 0 -and $Command -in @('build','flash')) {
    if ($UseUniversalCompiler) {
        & python (Join-Path $PSScriptRoot 'ninja_ldgen.py') --rewrite-build-dir $nexusBuildPath
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }
    & cmake --build $nexusBuildPath --parallel $Jobs 2>&1 |
        Tee-Object -FilePath (Join-Path $nexusRoot 'test-results\firmware-build.log') -Append
    $nexusExitCode = $LASTEXITCODE
}
if ($nexusExitCode -eq 0 -and $Command -eq 'flash') {
    & python (Join-Path $PSScriptRoot 'flash_firmware.py') --port $Port --build-dir $nexusBuildPath --backup-dir (Join-Path $nexusRoot 'test-results\device-backup') 2>&1 |
        Tee-Object -FilePath (Join-Path $nexusRoot 'test-results\firmware-flash.log') -Append
    $nexusExitCode = $LASTEXITCODE
}
$ErrorActionPreference = $nexusPreviousPreference
exit $nexusExitCode
