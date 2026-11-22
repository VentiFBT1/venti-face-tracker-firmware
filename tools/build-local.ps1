# Local Windows build. Run from an ESP-IDF v5.4.2 PowerShell terminal.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
    if (-not (Get-Command idf.py -ErrorAction SilentlyContinue)) { throw 'Open an ESP-IDF v5.4.2 terminal first.' }
    $env = @{}
    Get-Content (Join-Path $root 'upstream.env') | Where-Object { $_ -match '=' } | ForEach-Object { $k, $v = $_ -split '=', 2; $env[$k] = $v }

    if (Test-Path work) { Remove-Item -Recurse -Force work }
    $zip = Join-Path $root 'upstream.zip'
    Invoke-WebRequest -Uri "https://github.com/$($env.UPSTREAM_REPO)/archive/$($env.UPSTREAM_SHA).zip" -OutFile $zip
    Expand-Archive -Path $zip -DestinationPath $root
    Rename-Item -Path ("{0}-{1}" -f ($env.UPSTREAM_REPO -split '/')[1], $env.UPSTREAM_SHA) -NewName 'work'
    Remove-Item $zip

    Copy-Item -Path (Join-Path $root 'overlay\*') -Destination (Join-Path $root 'work') -Recurse -Force

    Push-Location work
    try {
        & python tools/switchBoardType.py --board $env.BOARD --clear-wifi
        if ($LASTEXITCODE -ne 0) { throw 'Board configuration failed' }
        & idf.py build
        if ($LASTEXITCODE -ne 0) { throw 'Compilation failed' }
        & idf.py merge-bin -f raw
        if ($LASTEXITCODE -ne 0) { throw 'Merging failed' }
        Get-FileHash -LiteralPath 'build\merged-binary.bin' -Algorithm SHA256
        Write-Output "Firmware: $root\work\build\merged-binary.bin"
    } finally { Pop-Location }
} finally { Pop-Location }
