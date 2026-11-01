[CmdletBinding()]
param(
    [string]$Compiler = "gcc",
    [switch]$Test,
    [switch]$Optimize,
    [string]$OutputName = "neo.exe"
)
$ErrorActionPreference = "Stop"
Push-Location $PSScriptRoot
try {
    if ($OutputName -notmatch '^[a-zA-Z0-9_-]+\.exe$') { throw 'OutputName must be a plain executable filename.' }
    $outputDirectory = Join-Path $PSScriptRoot "build/x86_64/Windows"
    New-Item -ItemType Directory -Force $outputDirectory | Out-Null
    $outputFile = Join-Path $outputDirectory $OutputName
    $temporaryFile = Join-Path $outputDirectory ($OutputName + '.tmp.exe')
    $sources = @(
        "neo-vm/main.c", "neo-vm/cli/cli.c", "neo-vm/cli/launch.c",
        "neo-vm/cli/selftest.c", "neo-vm/cli/live.c", "neo-vm/objects/objects.c",
        "neo-vm/messages/messages.c", "neo-vm/image/image.c",
        "neo-vm/packages/packages.c", "neo-vm/replication/replication.c",
        "neo-vm/execution/evaluator.c", "neo-vm/execution/graph.c", "neo-vm/execution/text_ops.c",
        "neo-vm/execution/message_ops.c", "neo-vm/execution/scheduler.c",
        "neo-vm/display/display.c", "neo-vm/display/headless.c", "neo-vm/display/text.c", "neo-vm/io/io.c", "neo-vm/platform/platform.c", "neo-vm/platform/network.c",
        "neo-vm/platform/x86_64/Windows/platform.c",
        "neo-vm/platform/win32/tls_selftest.c", "neo-vm/platform/win32/tls.c", "neo-vm/platform/win32/network.c", "neo-vm/platform/win32/streams.c", "neo-vm/platform/win32/window.c"
    )
    $optimization = if ($Optimize) { "-O2" } else { "-O0" }
    & $Compiler -std=c17 $optimization -g -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow -Wstrict-prototypes -Ineo-vm -DNEO_PLATFORM_WITH_WIN32 @sources -luser32 -lgdi32 -lshell32 -lws2_32 -lsecur32 -lcrypt32 -lncrypt -o $temporaryFile
    if ($LASTEXITCODE -ne 0) { throw "Windows compilation failed (exit $LASTEXITCODE)." }
    try {
        Move-Item -LiteralPath $temporaryFile -Destination $outputFile -Force
    } catch {
        throw "Could not replace $OutputName. If it is running, close it and rerun this build. The compiled temporary file remains at $temporaryFile. $($_.Exception.Message)"
    }
    Write-Output "Built $outputFile"
    if ($Test) {
        & $outputFile selftest
        if ($LASTEXITCODE -ne 0) { throw "Selftest failed (exit $LASTEXITCODE)." }
    }
} finally {
    Pop-Location
}
