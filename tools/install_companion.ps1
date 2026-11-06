param(
    [string]$SourceDirectory = (Join-Path (Split-Path -Parent $PSScriptRoot) 'dist\NexusDeck'),
    [switch]$NoStartup,
    [switch]$NoLaunch,
    [switch]$StopLegacyInstance
)
$ErrorActionPreference = 'Stop'
if (-not $env:LOCALAPPDATA) { throw 'Install from the intended Windows user account.' }
$nexusSource = (Resolve-Path -LiteralPath $SourceDirectory).Path
$nexusInfo = Get-Content -LiteralPath (Join-Path $nexusSource 'package-info.json') -Raw | ConvertFrom-Json
if ($nexusInfo.name -ne 'NEXUS DECK' -or $nexusInfo.version -notmatch '^\d+\.\d+\.\d+$') {
    throw 'Not a valid NEXUS DECK package. Build companion/NexusDeck.spec first.'
}
$nexusSourceExe = Join-Path $nexusSource 'NexusDeck.exe'
if (-not (Test-Path -LiteralPath $nexusSourceExe -PathType Leaf)) { throw 'NexusDeck.exe is missing.' }
$nexusRoot = [IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA 'Programs\NexusDeck'))
function Assert-NexusChild([string]$Path) {
    $absolute = [IO.Path]::GetFullPath($Path)
    if (-not $absolute.StartsWith($nexusRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Path escapes the installation directory: $absolute"
    }
}
if ((Test-Path -LiteralPath $nexusRoot) -and
    ((Get-Item -LiteralPath $nexusRoot).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
    throw 'The installation directory must not be a junction or symbolic link.'
}
$nexusEntries = Get-ChildItem -LiteralPath $nexusSource -Recurse -Force
if ($nexusEntries | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }) {
    throw 'The package must not contain junctions or symbolic links.'
}
New-Item -ItemType Directory -Path $nexusRoot -Force | Out-Null
$nexusStage = Join-Path $nexusRoot ('staging-' + [guid]::NewGuid().ToString('N'))
$nexusVersionDirectory = Join-Path $nexusRoot $nexusInfo.version
if (Test-Path -LiteralPath $nexusVersionDirectory) {
    $nexusVersionDirectory = Join-Path $nexusRoot ($nexusInfo.version + '-' + (Get-Date -Format 'yyyyMMddHHmmssfff'))
}
Assert-NexusChild $nexusStage
Assert-NexusChild $nexusVersionDirectory
New-Item -ItemType Directory -Path $nexusStage | Out-Null
foreach ($entry in Get-ChildItem -LiteralPath $nexusSource -Force) {
    Copy-Item -LiteralPath $entry.FullName -Destination $nexusStage -Recurse -Force
}
$nexusStagedExe = Join-Path $nexusStage 'NexusDeck.exe'
if ((Get-FileHash -LiteralPath $nexusSourceExe -Algorithm SHA256).Hash -ne
    (Get-FileHash -LiteralPath $nexusStagedExe -Algorithm SHA256).Hash) {
    throw 'Executable copy verification failed. The old installation is untouched.'
}
# Both resolved move targets were checked against the exact per-user program root.
Move-Item -LiteralPath $nexusStage -Destination $nexusVersionDirectory
$nexusExe = Join-Path $nexusVersionDirectory 'NexusDeck.exe'
Write-Output ('Verified NEXUS DECK ' + $nexusInfo.version + ' package copy.')

$nexusRunning = @(Get-CimInstance Win32_Process -Filter "Name = 'NexusDeck.exe'" | Where-Object {
    $_.ExecutablePath -eq $nexusSourceExe -or
    ($_.ExecutablePath -and $_.ExecutablePath.StartsWith($nexusRoot + '\', [StringComparison]::OrdinalIgnoreCase))
})
if ($nexusRunning.Count) {
    $nexusQuit = Start-Process -FilePath $nexusExe -ArgumentList '--quit' -WindowStyle Hidden -Wait -PassThru
    if ($nexusQuit.ExitCode -eq 3) { throw 'The running editor could not save its draft; installation was staged.' }
    foreach ($process in $nexusRunning) {
        $deadline = (Get-Date).AddSeconds(8)
        while ((Get-Process -Id $process.ProcessId -ErrorAction SilentlyContinue) -and (Get-Date) -lt $deadline) {
            Start-Sleep -Milliseconds 200
        }
        if (Get-Process -Id $process.ProcessId -ErrorAction SilentlyContinue) {
            if (-not $StopLegacyInstance -or $nexusQuit.ExitCode -ne 4) {
                throw 'Quit the older companion from its tray after saving, then retry the installation.'
            }
            # The opt-in flag applies only to legacy versions that have no quit IPC.
            $current = Get-CimInstance Win32_Process -Filter ('ProcessId = ' + $process.ProcessId)
            if ($current -and $current.ExecutablePath -eq $process.ExecutablePath -and $current.Name -eq 'NexusDeck.exe') {
                Stop-Process -Id $current.ProcessId -ErrorAction Stop
            }
        }
    }
}

$nexusShell = New-Object -ComObject WScript.Shell
function Save-NexusShortcut([string]$Path, [string]$Arguments, [int]$WindowStyle) {
    if (Test-Path -LiteralPath $Path) {
        $existing = $nexusShell.CreateShortcut($Path)
        if ($existing.TargetPath -and $existing.TargetPath -ne $nexusSourceExe -and
            -not $existing.TargetPath.StartsWith($nexusRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
            throw "An unrelated shortcut already uses this name: $Path"
        }
    }
    $shortcut = $nexusShell.CreateShortcut($Path)
    $shortcut.TargetPath = $nexusExe
    $shortcut.Arguments = $Arguments
    $shortcut.WorkingDirectory = $nexusVersionDirectory
    $shortcut.IconLocation = $nexusExe + ',0'
    $shortcut.WindowStyle = $WindowStyle
    $shortcut.Description = 'NEXUS DECK wireless macro controller'
    $shortcut.Save()
}
$nexusStartMenu = Join-Path ([Environment]::GetFolderPath('Programs')) 'NEXUS DECK.lnk'
$nexusStartup = Join-Path ([Environment]::GetFolderPath('Startup')) 'NEXUS DECK.lnk'
Save-NexusShortcut $nexusStartMenu '' 1
if (-not $NoStartup) { Save-NexusShortcut $nexusStartup '--background' 7 }
elseif (Test-Path -LiteralPath $nexusStartup) {
    $existingStartup = $nexusShell.CreateShortcut($nexusStartup)
    if ($existingStartup.TargetPath.StartsWith($nexusRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $nexusStartup -Force
    }
}
$nexusUninstall = Join-Path $nexusRoot 'uninstall.ps1'
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'uninstall_companion.ps1') -Destination $nexusUninstall -Force
$nexusManifest = @{
    name = 'NEXUS DECK'; version = $nexusInfo.version; executable = $nexusExe
    program_root = $nexusRoot; start_menu = $nexusStartMenu; startup = $nexusStartup
    startup_enabled = (-not $NoStartup); installed_at = (Get-Date).ToString('o')
}
$nexusManifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $nexusRoot 'install.json') -Encoding UTF8
$nexusRegistry = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\NexusDeck'
New-Item -Path $nexusRegistry -Force | Out-Null
$properties = @{
    DisplayName = 'NEXUS DECK'; DisplayVersion = $nexusInfo.version; Publisher = 'NEXUS DECK'
    InstallLocation = $nexusRoot; DisplayIcon = $nexusExe
    UninstallString = ('powershell.exe -NoProfile -ExecutionPolicy Bypass -File "' + $nexusUninstall + '"')
}
foreach ($property in $properties.GetEnumerator()) {
    New-ItemProperty -Path $nexusRegistry -Name $property.Key -Value $property.Value -PropertyType String -Force | Out-Null
}
foreach ($property in @('NoModify','NoRepair')) {
    New-ItemProperty -Path $nexusRegistry -Name $property -Value 1 -PropertyType DWord -Force | Out-Null
}
if (-not $NoLaunch) {
    $nexusStarted = Start-Process -FilePath $nexusExe -ArgumentList '--background' -WorkingDirectory $nexusVersionDirectory -WindowStyle Hidden -PassThru
    Write-Output ('Started NEXUS DECK in the tray, PID ' + $nexusStarted.Id)
}
Write-Output ('Installed at ' + $nexusVersionDirectory)
Write-Output ('Start with Windows: ' + (-not $NoStartup))
Write-Output 'Saved profiles, connection settings and Windows Bluetooth pairing were retained.'
