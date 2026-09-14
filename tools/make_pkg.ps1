# LUC - pack luc_modules\*.luc into packages\ai.lucpkg
# update 2026-09-01: commit packages\ai.lucpkg so "luc install ai" works
# The bundle is plain text so the built-in "luc install ai" command can
# download ONE file and split it locally (no zip support needed in C).
# Bundle format:
#   #=lucpkg:ai
#   #=lucfile: <name>
#   <content>
#   #=endpkg
# Run from the repo root:  powershell -File tools\make_pkg.ps1
$ErrorActionPreference = 'Stop'

$ProjectDir = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$SrcDir = Join-Path $ProjectDir 'luc_modules'
$OutFile = Join-Path $ProjectDir 'packages\ai.lucpkg'
$Marker = '#=lucfile: '

$files = Get-ChildItem -Path (Join-Path $SrcDir '*.luc') | Sort-Object Name
if (-not $files -or $files.Count -eq 0) {
    throw "no .luc files found in $SrcDir"
}

$sb = New-Object System.Text.StringBuilder
[void]$sb.Append("#=lucpkg:ai`n")

foreach ($f in $files) {
    $content = [IO.File]::ReadAllText($f.FullName)
    foreach ($line in ($content -split "`n")) {
        $t = $line.TrimEnd("`r")
        if ($t.StartsWith($Marker) -or $t.StartsWith('#=lucpkg:') -or $t.StartsWith('#=endpkg')) {
            throw "marker collision in $($f.Name): $t"
        }
    }
    if (-not $content.EndsWith("`n")) { $content += "`n" }
    [void]$sb.Append($Marker + $f.Name + "`n")
    [void]$sb.Append($content)
}

[void]$sb.Append("#=endpkg`n")
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $OutFile) | Out-Null
# utf8 without BOM, LF line endings
[IO.File]::WriteAllText($OutFile, $sb.ToString(), (New-Object System.Text.UTF8Encoding($false)))
Write-Host ("wrote {0}  ({1} modules)" -f $OutFile, $files.Count)
