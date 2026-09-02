[CmdletBinding()]
param(
  [string]$DatabasePath = (Join-Path $PSScriptRoot "..\demo\portfolio.db"),
  [string]$BinaryPath = ""
)

$ErrorActionPreference = "Stop"
if ([string]::IsNullOrWhiteSpace($BinaryPath)) {
  $candidates = @(
    (Join-Path $PSScriptRoot "..\build\release\bin\tuplestone.exe"),
    (Join-Path $PSScriptRoot "..\build\debug\bin\tuplestone.exe")
  )
  $BinaryPath = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
}
if ([string]::IsNullOrWhiteSpace($BinaryPath)) {
  throw "Build tuplestone first (cmake --preset release) or pass -BinaryPath."
}

$directory = Split-Path -Parent $DatabasePath
New-Item -ItemType Directory -Force -Path $directory | Out-Null
Remove-Item -LiteralPath $DatabasePath -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath ($DatabasePath + ".wal") -Force -ErrorAction SilentlyContinue

$script = @"
.help
CREATE TABLE projects (id INTEGER PRIMARY KEY, name TEXT NOT NULL, stars INTEGER)
INSERT INTO projects VALUES (1, 'storage', 42), (2, 'recovery', 57), (3, 'sql', NULL)
SELECT id, name, stars FROM projects WHERE stars IS NOT NULL ORDER BY stars DESC
BEGIN
INSERT INTO projects VALUES (4, 'uncommitted', 0)
ROLLBACK
SELECT COUNT(*) FROM projects
.tables
.schema projects
.quit
"@

$script | & $BinaryPath $DatabasePath
if ($LASTEXITCODE -ne 0) { throw "tuplestone demo failed with exit code $LASTEXITCODE" }
Write-Host "Demo database: $([System.IO.Path]::GetFullPath($DatabasePath))"
