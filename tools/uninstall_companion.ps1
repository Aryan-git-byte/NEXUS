$ErrorActionPreference = 'Stop'
if (-not $env:LOCALAPPDATA) { throw 'Run from the Windows account that installed NEXUS DECK.' }
$nexusRoot = [IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA 'Programs\NexusDeck'))
$nexusManifestFile = Join-Path $nexusRoot 'install.json'
if (-not (Test-Path -LiteralPath $nexusManifestFile -PathType Leaf)) { throw 'No NEXUS DECK installation manifest found.' }
$nexusManifest = Get-Content -LiteralPath $nexusManifestFile -Raw | ConvertFrom-Json
if ($nexusManifest.name -ne 'NEXUS DECK' -or $nexusManifest.program_root -ne $nexusRoot -or
    -not $nexusManifest.executable.StartsWith($nexusRoot + '\', [StringComparison]::OrdinalIgnoreCase) -or
    ((Get-Item -LiteralPath $nexusRoot).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
    throw 'Installation identity or path validation failed; nothing was removed.'
}
$nexusProcesses = @(Get-CimInstance Win32_Process -Filter "Name = 'NexusDeck.exe'" | Where-Object {
    $_.ExecutablePath -and $_.ExecutablePath.StartsWith($nexusRoot + '\', [StringComparison]::OrdinalIgnoreCase)
})
if ($nexusProcesses.Count) {
    $nexusQuit = Start-Process -FilePath $nexusManifest.executable -ArgumentList '--quit' -WindowStyle Hidden -Wait -PassThru
    if ($nexusQuit.ExitCode -ne 0) { throw 'Save your editor draft and quit NEXUS DECK from its tray before uninstalling.' }
    foreach ($process in $nexusProcesses) {
        $deadline = (Get-Date).AddSeconds(10)
        while ((Get-Process -Id $process.ProcessId -ErrorAction SilentlyContinue) -and (Get-Date) -lt $deadline) {
            Start-Sleep -Milliseconds 200
        }
        if (Get-Process -Id $process.ProcessId -ErrorAction SilentlyContinue) { throw 'Companion is still shutting down; retry shortly.' }
    }
}
$nexusShell = New-Object -ComObject WScript.Shell
foreach ($shortcutPath in @(
    (Join-Path ([Environment]::GetFolderPath('Programs')) 'NEXUS DECK.lnk'),
    (Join-Path ([Environment]::GetFolderPath('Startup')) 'NEXUS DECK.lnk')
)) {
    if (Test-Path -LiteralPath $shortcutPath) {
        $shortcut = $nexusShell.CreateShortcut($shortcutPath)
        if ($shortcut.TargetPath.StartsWith($nexusRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
            Remove-Item -LiteralPath $shortcutPath -Force
        }
    }
}
Remove-Item -LiteralPath 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\NexusDeck' -Recurse -ErrorAction SilentlyContinue
# Resolve and verify the exact named program root immediately before recursive removal.
$nexusResolvedRoot = (Resolve-Path -LiteralPath $nexusRoot).Path
if (-not [string]::Equals($nexusResolvedRoot, $nexusRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The resolved program directory changed; removal stopped.'
}
Remove-Item -LiteralPath $nexusResolvedRoot -Recurse -Force
Write-Output 'NEXUS DECK was uninstalled. Your profiles/settings in LocalAppData\NexusDeck and Bluetooth pairing were retained.'
