[CmdletBinding()]
param([string]$ExecutableName = 'neo.exe', [string]$ImageName = 'image.neo')
$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    if ($ExecutableName -notmatch '^[a-zA-Z0-9_-]+\.exe$' -or $ImageName -notmatch '^[a-zA-Z0-9_-]+\.neo$') { throw 'Use plain executable and image filenames.' }
    $neoExecutable = Join-Path $PSScriptRoot ("build/x86_64/Windows/" + $ExecutableName)
    if (-not (Test-Path -LiteralPath $neoExecutable)) { throw 'Build neo with build-windows.ps1 first.' }
    $guiCommands = [System.Collections.Generic.List[string]]::new()
    foreach ($packageName in @('canvas', 'package-browser', 'vector', 'geometry', 'projection', 'sketch', 'text-editor', 'task-manager')) {
        $guiCommands.Add("pkg-load neo/packages/$packageName.neo")
        $guiCommands.Add('pkg install')
        $guiCommands.Add('pkg clear')
    }
    $guiCommands.Add("save build/x86_64/Windows/$ImageName")
    $guiCommands.Add('quit')
    $guiOutput = ($guiCommands -join "`n") | & $neoExecutable live neo/neo-os/image.neo 2>&1
    $guiExitCode = $LASTEXITCODE
    $guiOutput | Set-Content -LiteralPath 'build/x86_64/Windows/desktop-build.log' -Encoding utf8
    if ($guiExitCode -ne 0 -or ($guiOutput | Select-String 'neo: ')) { throw 'GUI image preparation failed; see desktop-build.log.' }
    Write-Output "Prepared $PSScriptRoot/build/x86_64/Windows/$ImageName"
    Write-Output "Launch: ./build/x86_64/Windows/$ExecutableName --gui build/x86_64/Windows/$ImageName"
} finally {
    Pop-Location
}
