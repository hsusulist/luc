# LUC - pack luc_modules\*.luc into packages\ai.luic + packages\discord.luic
# "luc install -p ai|discord" downloads ONE file from GitHub and splits it locally
# (no zip support needed in C). The file is a .luic container, the same framing
# "luc pack" uses:
#   !luic1
#   lib <name>
#   parts <n>
#   ==== <file name> <byte size>
#   <exactly that many bytes>
#   (repeated per file)
# Sizes are byte counts, so any file content is safe and a cut-off download is detected.
# discord*.luc ships in its own package (not inside ai.luic); http.luc comes with
# the interpreter, so it is not in ai.luic either.
# Run from the repo root:  powershell -File tools\make_pkg.ps1   (build_installer.ps1 does it too)
$ErrorActionPreference = 'Stop'

$ProjectDir = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$SrcDir = Join-Path $ProjectDir 'luc_modules'
function Pack-Bundle([string]$OutFile, [string]$PkgName, [object[]]$Files) {
    if (-not $Files -or $Files.Count -eq 0) {
        Write-Host ("skip {0}: no source files" -f $OutFile)
        return
    }
    $ms = New-Object System.IO.MemoryStream
    $head = [Text.Encoding]::ASCII.GetBytes("!luic1`nlib $PkgName`nparts $($Files.Count)`n")
    $ms.Write($head, 0, $head.Length)
    foreach ($f in $Files) {
        if ($f.Name -notmatch '^[A-Za-z0-9_.\-]+$') { throw "bad file name for a package: $($f.Name)" }
        $bytes = [IO.File]::ReadAllBytes($f.FullName)
        $h = [Text.Encoding]::ASCII.GetBytes("==== " + $f.Name + " " + $bytes.Length + "`n")
        $ms.Write($h, 0, $h.Length)
        $ms.Write($bytes, 0, $bytes.Length)
        $ms.WriteByte(10)
    }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $OutFile) | Out-Null
    [IO.File]::WriteAllBytes($OutFile, $ms.ToArray())
    Write-Host ("wrote {0}  ({1} files)" -f $OutFile, $Files.Count)
}

$all = Get-ChildItem -Path (Join-Path $SrcDir '*.luc') | Sort-Object Name
if (-not $all -or $all.Count -eq 0) {
    throw "no .luc files found in $SrcDir"
}

# http.luc ships with the interpreter (installer main component), not with ai
$aiFiles = @($all | Where-Object { $_.Name -notlike 'discord*' -and $_.Name -ne 'http.luc' })
Pack-Bundle (Join-Path $ProjectDir 'packages\ai.luic') 'ai' $aiFiles

$dcFiles = @($all | Where-Object { $_.Name -like 'discord*' })
Pack-Bundle (Join-Path $ProjectDir 'packages\discord.luic') 'discord' $dcFiles
