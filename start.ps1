param(
    [string]$Compiler = '',
    [string]$Python = 'python',
    [ValidateRange(1024,65535)][int]$Port = 8765,
    [switch]$BuildOnly
)
$ErrorActionPreference = 'Stop'
Push-Location -LiteralPath $PSScriptRoot
try {
    if ([string]::IsNullOrWhiteSpace($Compiler)) {
        if (Test-Path -LiteralPath 'C:/MinGW/bin/gcc.exe') {
            $Compiler = 'C:/MinGW/bin/gcc.exe'
        } else {
            $Compiler = (Get-Command gcc -ErrorAction Stop).Source
        }
    }
    [void](New-Item -ItemType Directory -Path './build' -Force)
    $taskSources = @((Get-ChildItem -LiteralPath './src' -Filter '*.c').FullName)
    if (Test-Path -LiteralPath './vendor/bosch') {
        $taskSources += @((Get-ChildItem -LiteralPath './vendor/bosch' -Filter '*.c').FullName)
    }
    & $Compiler -std=c17 -Wall -Wextra -Werror -pedantic -O2 -Iinclude @taskSources './app/simulator.c' -lm -o './build/whm_simulator.exe'
    if ($LASTEXITCODE -ne 0) { throw 'The C simulator could not be compiled.' }
    if ($BuildOnly) { return }
    Write-Host ('Open http://127.0.0.1:{0} in your browser. Press Ctrl+C to stop.' -f $Port)
    & $Python -B './app/dashboard_server.py' --host 127.0.0.1 --port $Port
    if ($LASTEXITCODE -ne 0) { throw 'The dashboard server could not be started.' }
} finally {
    Pop-Location
}
