# LUC - migrate all .luc files from Lua 'local' to LUC 'create'
# Run from the repo root:  powershell -File tools\migrate_create.ps1
# Word-boundary replace, so "locals" or "mylocal" stay untouched.
# The one 'local' inside a help string in luc_modules is a usage
# example, replacing it is correct.
$ErrorActionPreference = 'Stop'

$files = Get-ChildItem -Path . -Recurse -Filter *.luc |
    Where-Object { $_.FullName -notmatch '\\\.git\\' }
if (-not $files) { throw 'no .luc files found' }

$total = 0
foreach ($f in $files) {
    $text = [IO.File]::ReadAllText($f.FullName)
    $n = [regex]::Matches($text, '\blocal\b').Count
    if ($n -gt 0) {
        [IO.File]::WriteAllText($f.FullName, [regex]::Replace($text, '\blocal\b', 'create'),
            (New-Object System.Text.UTF8Encoding($false)))
        $total += $n
        Write-Host ("{0}: {1} replaced" -f $f.FullName.Substring((Get-Location).Path.Length + 1), $n)
    }
}
Write-Host "done - $total replacement(s) in $($files.Count) file(s)"
Write-Host 'now rebuild: run build_installer.ps1 (or just recompile luc)'
