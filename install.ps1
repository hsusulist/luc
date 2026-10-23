[CmdletBinding()]
param(
    [string]$InstallPath,
    [string[]]$Components,
    [string]$ArchivePath,
    [ValidateSet('github', 'local')]
    [string]$Source,
    [switch]$Yes,
    [switch]$NoPath,
    [switch]$NoEnvironmentChanges,
    [switch]$Force,
    [switch]$Quiet,
    [switch]$AllowUnsigned,
    [switch]$NoColor
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Numerics
Add-Type -AssemblyName System.IO.Compression.FileSystem

$repo = 'hsusulist/luc'
$minisignPublicKey = 'RWRPLJnLdrwqwkmhtMPAEfihwSnAC9TmbPc4ghGntXYOSLqKlKGUUx40'
$defaultInstallPath = Join-Path $env:LOCALAPPDATA 'Programs\LUC'
$optionalComponents = @('window', 'ai', 'discord', 'lcode', 'vscode', 'source')
$componentInfo = [ordered]@{
    window  = @{ Label = 'Window support';   Desc = 'SDL2 graphics, sound and the Pong demo' }
    ai      = @{ Label = 'AI libraries';     Desc = 'Tensor, transformer, training and tokenizer modules' }
    discord = @{ Label = 'Discord library';  Desc = 'Discord bot library and demos' }
    lcode   = @{ Label = 'LCode support';    Desc = 'LCode editor plugin and Nerd Font' }
    vscode  = @{ Label = 'VS Code extension'; Desc = 'Syntax highlighting and snippets for VS Code' }
    source  = @{ Label = 'C source files';   Desc = 'Source code for the interpreter' }
}

$maxEntries = 4096
$maxEntryBytes = 512MB
$maxTotalBytes = 2GB
$maxCompressionRatio = 200
$maxPathLength = 240

$script:Esc = [char]27
$script:UseAnsi = $false
$script:InAltScreen = $false
$script:Interactive = $false
$script:Version = ''

# ---------------------------------------------------------------------------
# Console helpers
# ---------------------------------------------------------------------------

function Initialize-Console {
    try {
        [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
        [Console]::InputEncoding = [Text.UTF8Encoding]::new($false)
    } catch { }
    if ($NoColor -or $env:NO_COLOR) { return }
    try {
        if (-not ('LucNative.ConsoleApi' -as [type])) {
            Add-Type -Namespace LucNative -Name ConsoleApi -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("kernel32.dll", SetLastError = true)]
public static extern System.IntPtr GetStdHandle(int nStdHandle);
[System.Runtime.InteropServices.DllImport("kernel32.dll", SetLastError = true)]
public static extern bool GetConsoleMode(System.IntPtr hConsoleHandle, out uint lpMode);
[System.Runtime.InteropServices.DllImport("kernel32.dll", SetLastError = true)]
public static extern bool SetConsoleMode(System.IntPtr hConsoleHandle, uint dwMode);
[System.Runtime.InteropServices.DllImport("kernel32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode, SetLastError = true)]
public static extern bool MoveFileEx(string existingFileName, string newFileName, int flags);
'@
        }
        $handle = [LucNative.ConsoleApi]::GetStdHandle(-11)
        $mode = [uint32]0
        if ([LucNative.ConsoleApi]::GetConsoleMode($handle, [ref]$mode)) {
            $script:UseAnsi = [LucNative.ConsoleApi]::SetConsoleMode($handle, $mode -bor 4)
        }
    } catch { $script:UseAnsi = $false }
}

function Enter-AltScreen {
    if ($script:UseAnsi) {
        [Console]::Write("$($script:Esc)[?1049h$($script:Esc)[?25l")
        $script:InAltScreen = $true
    }
}

function Exit-AltScreen {
    if ($script:InAltScreen) {
        [Console]::Write("$($script:Esc)[0m$($script:Esc)[?25h$($script:Esc)[?1049l")
        $script:InAltScreen = $false
    }
}

function Clear-Tui {
    if ($script:UseAnsi) { [Console]::Write("$($script:Esc)[2J$($script:Esc)[H") }
    else { Clear-Host }
}

function Color-Text {
    param([string]$Text, [string]$Code = '')
    if (-not $script:UseAnsi -or -not $Code) { return $Text }
    return "$($script:Esc)[$($Code)m$Text$($script:Esc)[0m"
}

function Write-Tui {
    param([string]$Text = '', [string]$Code = '')
    [Console]::Write((Color-Text $Text $Code) + "`n")
}

function Get-TuiSize {
    $w = 80
    $h = 25
    try {
        if ([Console]::WindowWidth -gt 0) { $w = [Console]::WindowWidth }
        if ([Console]::WindowHeight -gt 0) { $h = [Console]::WindowHeight }
    } catch { }
    return @{ W = [Math]::Max(40, $w - 2); H = [Math]::Max(16, $h - 1) }
}

function Write-TuiHeader {
    param([string]$Step, [string]$Title)
    $size = Get-TuiSize
    $line = [string]::new([char]0x2500, $size.W)
    Write-Tui ''
    Write-Tui ('  LUC {0}  installer' -f $script:Version) '1;36'
    Write-Tui ("  $Step  $([char]0x00B7)  $Title") '90'
    Write-Tui $line '90'
    Write-Tui ''
}

function Write-TuiFooter {
    param([string]$Text)
    $size = Get-TuiSize
    Write-Tui ''
    Write-Tui ([string]::new([char]0x2500, $size.W)) '90'
    Write-Tui ("  $Text") '90'
}

function Show-Banner {
    if ($Quiet) { return }
    $display = $script:Version -replace '-beta([0-9]+)$', ' beta $1' -replace '-rc([0-9]+)$', ' rc $1'
    if (-not $display) { $display = 'installer' }
    Write-Host ''
    Write-Host "██╗     ██╗   ██╗ ██████╗  Version: $display" -ForegroundColor Cyan
    Write-Host '██║     ██║   ██║██╔════╝  License: Apache 2.0' -ForegroundColor Cyan
    Write-Host '██║     ██║   ██║██║       Repo: https://github.com/hsusulist/luc' -ForegroundColor Cyan
    Write-Host '██║     ██║   ██║██║       Welcome to luc.' -ForegroundColor Cyan
    Write-Host '██████╗╚██████╔╝╚██████╗   Installer for Windows' -ForegroundColor Cyan
    Write-Host '╚══════╝ ╚═════╝  ╚═════╝' -ForegroundColor Cyan
    Write-Host ''
}

function Write-Step {
    param([string]$Message)
    if ($Quiet) { return }
    Write-Host ("  [{0}] {1}" -f (Color-Text '..' '36'), $Message) -NoNewline
}

function Write-StepDone {
    param([string]$Message)
    if ($Quiet) { return }
    Write-Host "`r  [$([char]0x2713)] $Message"
}

function Write-StepFail {
    param([string]$Message)
    if ($Quiet) { return }
    Write-Host "`r  [x] $Message" -ForegroundColor Red
}

function Write-Note {
    param([string]$Message, [string]$Kind = 'info')
    if ($Quiet -and $Kind -eq 'info') { return }
    switch ($Kind) {
        'warn' { Write-Host "  ! $Message" -ForegroundColor Yellow }
        'ok'   { Write-Host "  $([char]0x2713) $Message" -ForegroundColor Green }
        'err'  { Write-Host "  x $Message" -ForegroundColor Red }
        default { Write-Host "  $Message" }
    }
}

# ---------------------------------------------------------------------------
# Interactive pages
# ---------------------------------------------------------------------------

function Show-LicensePage {
    param([string]$Text)
    if ([string]::IsNullOrWhiteSpace($Text)) {
        $Text = 'LUC is free software released under the Apache License 2.0.'
    }
    $size = Get-TuiSize
    $width = $size.W - 6
    $wrapped = New-Object System.Collections.Generic.List[string]
    foreach ($raw in ($Text -split "`r?`n")) {
        if ($raw.Length -eq 0) { $wrapped.Add(''); continue }
        $words = $raw -split '\s+'
        $current = ''
        foreach ($word in $words) {
            if ($current.Length -eq 0) { $current = $word }
            elseif (($current.Length + 1 + $word.Length) -le $width) { $current += ' ' + $word }
            else { $wrapped.Add($current); $current = $word }
        }
        if ($current.Length -gt 0) { $wrapped.Add($current) }
    }
    $pageSize = [Math]::Max(6, $size.H - 10)
    $pages = [Math]::Max(1, [Math]::Ceiling($wrapped.Count / $pageSize))
    $page = 0
    while ($true) {
        Clear-Tui
        Write-TuiHeader 'License' 'Apache License 2.0'
        $start = $page * $pageSize
        $end = [Math]::Min($wrapped.Count, $start + $pageSize)
        for ($i = $start; $i -lt $end; $i++) {
            Write-Tui ("  " + $wrapped[$i])
        }
        Write-TuiFooter ("Page {0}/{1}   Up/Down, PgUp/PgDn scroll   Enter/Esc back" -f ($page + 1), $pages)
        $key = [Console]::ReadKey($true)
        switch ($key.Key) {
            'UpArrow'   { if ($page -gt 0) { $page-- } }
            'DownArrow' { if ($page -lt $pages - 1) { $page++ } }
            'PageUp'    { $page = [Math]::Max(0, $page - 1) }
            'PageDown'  { $page = [Math]::Min($pages - 1, $page + 1) }
            'Enter'     { return }
            'Escape'    { return }
            'Q'         { return }
        }
    }
}

function Show-SourcePage {
    $items = @(
        @{ Label = 'GitHub release'; Desc = 'Download the latest signed portable release (recommended)' },
        @{ Label = 'Local archive';  Desc = 'Use a luc-portable.zip already on this computer' }
    )
    $index = 0
    while ($true) {
        Clear-Tui
        Write-TuiHeader 'Source' 'Installation method'
        for ($i = 0; $i -lt $items.Count; $i++) {
            $marker = if ($i -eq $index) { '>' } else { ' ' }
            $code = if ($i -eq $index) { '1;36' } else { '' }
            Write-Tui ("  $marker " + $items[$i].Label) $code
            Write-Tui ("      " + $items[$i].Desc) '90'
            Write-Tui ''
        }
        Write-TuiFooter 'Up/Down move   Enter select   q quit'
        $key = [Console]::ReadKey($true)
        switch ($key.Key) {
            'UpArrow'   { $index = ($index - 1 + $items.Count) % $items.Count }
            'DownArrow' { $index = ($index + 1) % $items.Count }
            'Enter'     { return $index }
            'Escape'    { return -1 }
            'Q'         { return -1 }
        }
    }
}

function Show-LicenseEntry {
    param([string]$LicenseText)
    Show-LicensePage $LicenseText
}

function Show-ComponentPage {
    param([string[]]$Preselected, [string]$LicenseText)

    $selected = @{}
    foreach ($name in $Preselected) { $selected[$name] = $true }
    $index = 0
    while ($true) {
        Clear-Tui
        Write-TuiHeader 'Components' 'Choose what to install'
        Write-Tui ('  ' + (Color-Text '[x]' '32') + ' Core interpreter            Required') '32'
        Write-Tui ''
        $i = 0
        foreach ($name in $optionalComponents) {
            $info = $componentInfo[$name]
            $marker = if ($i -eq $index) { '>' } else { ' ' }
            $box = if ($selected[$name]) { '[x]' } else { '[ ]' }
            $code = if ($i -eq $index) { '1;36' } else { '' }
            Write-Tui ("  $marker " + $box + ' ' + $info.Label) $code
            Write-Tui ("        " + $info.Desc) '90'
            $i++
        }
        Write-Tui ''
        $chosen = @($optionalComponents | Where-Object { $selected[$_] })
        Write-Tui ("  Selected: core" + $(if ($chosen.Count) { ', ' + ($chosen -join ', ') } else { '' })) '90'
        Write-TuiFooter 'Up/Down move   Space toggle   a all   n none   l license   Enter continue   q quit'
        $key = [Console]::ReadKey($true)
        switch ($key.Key) {
            'UpArrow'    { $index = ($index - 1 + $optionalComponents.Count) % $optionalComponents.Count }
            'DownArrow'  { $index = ($index + 1) % $optionalComponents.Count }
            'Spacebar'   { $selected[$optionalComponents[$index]] = -not $selected[$optionalComponents[$index]] }
            'Enter'      { return @($optionalComponents | Where-Object { $selected[$_] }) }
            'Escape'     { return @($optionalComponents | Where-Object { $selected[$_] }) }
            'Q'          { return $null }
        }
        if ($key.KeyChar -eq 'a' -or $key.KeyChar -eq 'A') {
            foreach ($name in $optionalComponents) { $selected[$name] = $true }
        } elseif ($key.KeyChar -eq 'n' -or $key.KeyChar -eq 'N') {
            $selected = @{}
        } elseif ($key.KeyChar -eq 'l' -or $key.KeyChar -eq 'L') {
            Exit-AltScreen
            Show-LicensePage $LicenseText
            Enter-AltScreen
        }
    }
}

function Show-PathPage {
    param([string]$Title, [string]$Default, [switch]$ExistingFile)
    while ($true) {
        Clear-Tui
        Write-TuiHeader 'Location' $Title
        Write-Tui '  Enter a folder, or leave empty for the default:' '90'
        Write-Tui ("  " + $Default) '36'
        Write-Tui ''
        Write-Tui '  q to cancel' '90'
        Write-Tui ''
        [Console]::Write((Color-Text '  > ' '1;36'))
        $line = [Console]::ReadLine()
        if ($null -eq $line -or $line.Trim() -eq 'q' -or $line.Trim() -eq 'Q') { return $null }
        if ([string]::IsNullOrWhiteSpace($line)) { $line = $Default }
        try {
            if ($ExistingFile) {
                if (-not (Test-Path -LiteralPath $line -PathType Leaf)) { throw "File not found: $line" }
                return (Resolve-Path -LiteralPath $line).Path
            }
            return (Resolve-InstallPathValue $line)
        } catch {
            Write-Tui ("  " + $_.Exception.Message) '31'
            Write-Tui '  Press any key to try again.' '90'
            [Console]::ReadKey($true) | Out-Null
        }
    }
}

function Show-SummaryPage {
    param([object]$Plan)
    $focus = 0
    while ($true) {
        Clear-Tui
        Write-TuiHeader 'Summary' 'Review and continue'
        Write-Tui ("  Version      " + (Color-Text $Plan.Version '36'))
        Write-Tui ("  Source       " + $Plan.Source)
        Write-Tui ("  Folder       " + $Plan.InstallPath)
        Write-Tui ("  Components   core" + $(if ($Plan.Components.Count) { ', ' + ($Plan.Components -join ', ') } else { '' }))
        Write-Tui ("  PATH         " + $(if ($NoPath) { 'do not modify' } else { 'add to user PATH' }))
        if ($Plan.Upgrade) { Write-Tui '' ; Write-Tui '  LUC is already installed here; managed files will be replaced.' '33' }
        Write-Tui ''
        $buttons = @('Continue', 'Back')
        $rendered = @()
        for ($i = 0; $i -lt $buttons.Count; $i++) {
            if ($i -eq $focus) { $rendered += (Color-Text ("[ $($buttons[$i]) ]") '1;30;47') }
            else { $rendered += ("[ $($buttons[$i]) ]") }
        }
        Write-Tui ('      ' + ($rendered -join '        '))
        Write-TuiFooter 'Left/Right/Tab move   Enter select   Esc back'
        $key = [Console]::ReadKey($true)
        switch ($key.Key) {
            'LeftArrow'  { $focus = ($focus - 1 + $buttons.Count) % $buttons.Count }
            'RightArrow' { $focus = ($focus + 1) % $buttons.Count }
            'Tab'        { $focus = ($focus + 1) % $buttons.Count }
            'Escape'     { return 'back' }
            'Enter'      { return $(if ($focus -eq 0) { 'continue' } else { 'back' }) }
        }
    }
}

# ---------------------------------------------------------------------------
# Validation and archive helpers
# ---------------------------------------------------------------------------

function Resolve-InstallPathValue {
    param([string]$RequestedPath)
    if ([string]::IsNullOrWhiteSpace($RequestedPath)) { $RequestedPath = $defaultInstallPath }
    $expanded = [Environment]::ExpandEnvironmentVariables($RequestedPath.Trim())
    if ($expanded -eq '~') { $expanded = $env:USERPROFILE }
    elseif ($expanded.StartsWith('~\')) { $expanded = Join-Path $env:USERPROFILE $expanded.Substring(2) }
    if ($expanded -match '[\x00-\x1F]') { throw 'The install folder contains control characters.' }
    # A trailing separator survives command-line quoting as a doubled
    # backslash and confuses GetFullPath, so drop it unless this is a root.
    try {
        $expandedRoot = [IO.Path]::GetPathRoot($expanded)
        if ($expandedRoot -and
            $expanded.TrimEnd([char]92) -ne $expandedRoot.TrimEnd([char]92)) {
            $expanded = $expanded.TrimEnd([char]92)
        }
    } catch { }
    $full = [IO.Path]::GetFullPath($expanded)
    $root = [IO.Path]::GetPathRoot($full)
    if ($full.TrimEnd([char]92) -eq $root.TrimEnd([char]92)) {
        throw 'Choose a folder below a drive root, not the drive root itself.'
    }
    $full = $full.TrimEnd([char]92)
    $blocked = @(
        (Join-Path $env:WINDIR 'System32'),
        $env:WINDIR,
        $env:ProgramFiles,
        ${env:ProgramFiles(x86)}
    ) | Where-Object { $_ }
    foreach ($item in $blocked) {
        if ($full.TrimEnd([char]92).Equals($item.TrimEnd([char]92), [StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to install directly into a protected system folder: $full"
        }
    }
    if ($full.Length -ge ($maxPathLength - 40)) {
        throw 'The install folder is too long. Choose a shorter path.'
    }
    return $full
}

function Assert-SafeRelativePath {
    param([string]$RelativePath)
    $parts = $RelativePath -split '/'
    foreach ($part in $parts) {
        if ($part.Length -eq 0 -or $part -eq '.' -or $part -eq '..') {
            throw "Unsafe path in portable archive: $RelativePath"
        }
        if ($part -match '[<>:"|?*\x00-\x1F]') {
            throw "Invalid character in archive path: $RelativePath"
        }
        if ($part.EndsWith('.') -or $part.EndsWith(' ')) {
            throw "Invalid trailing dot or space in archive path: $RelativePath"
        }
        if ($part.Length -gt 255) {
            throw "Archive path segment is too long: $RelativePath"
        }
        $stem = ($part -split '\.')[0].ToUpperInvariant()
        if ($stem -match '^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])$') {
            throw "Reserved Windows device name in archive: $RelativePath"
        }
    }
}

function Assert-SafeDestination {
    param([string]$RelativePath, [string]$Root)
    Assert-SafeRelativePath $RelativePath
    $destination = [IO.Path]::GetFullPath((Join-Path $Root ($RelativePath.Replace('/', '\'))))
    $prefix = $Root.TrimEnd('\') + '\'
    if (-not $destination.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Archive entry escapes the install folder: $RelativePath"
    }
    if ($destination.Length -ge $maxPathLength) {
        throw "Installed path would be too long: $destination"
    }
    return $destination
}

function Read-ZipEntryText {
    param([IO.Compression.ZipArchive]$Zip, [string]$Name)
    $entry = $Zip.GetEntry($Name)
    if (-not $entry) { return $null }
    $reader = [IO.StreamReader]::new($entry.Open())
    try { return $reader.ReadToEnd() }
    finally { $reader.Dispose() }
}

function Read-ZipVersion {
    param([IO.Compression.ZipArchive]$Zip)
    $version = Read-ZipEntryText $Zip 'VERSION.txt'
    if ([string]::IsNullOrWhiteSpace($version)) { throw 'Portable archive has no VERSION.txt.' }
    return $version.Trim()
}

function Get-ArchivePlan {
    param(
        [IO.Compression.ZipArchive]$Zip,
        [string[]]$SelectedComponents,
        [string]$Target,
        [string]$VsCodeTarget
    )

    if ($Zip.Entries.Count -gt $maxEntries) {
        throw "Archive contains too many entries ($($Zip.Entries.Count) > $maxEntries)."
    }

    $prefixes = @('core', 'common')
    foreach ($name in $SelectedComponents) {
        if ($name -ne 'core') { $prefixes += $name }
    }

    $plan = New-Object System.Collections.Generic.List[object]
    $seen = @{}
    $totalBytes = [long]0
    $compressedBytes = [long]0

    foreach ($entry in $Zip.Entries) {
        $full = $entry.FullName.Replace('\', '/')
        if ([string]::IsNullOrEmpty($full)) { continue }

        $mode = ($entry.ExternalAttributes -shr 16) -band 0xF000
        if ($mode -eq 0xA000) { throw "Symbolic links are not allowed in the archive: $full" }
        if ($full.EndsWith('/')) { continue }

        $rootKind = $null
        $relative = $null
        $prefix = ''
        $priority = 0
        if ($full -match '^(core|common|window|ai|discord|lcode|source)/(.+)$') {
            $prefix = $Matches[1]
            $relative = $Matches[2]
            if ($prefixes -notcontains $prefix) { continue }
            $rootKind = 'app'
            if ($prefix -eq 'common') { $priority = 1 }
            elseif ($prefix -ne 'core') { $priority = 2 }
        } elseif ($full -match '^vscode/(.+)$') {
            if ($SelectedComponents -notcontains 'vscode') { continue }
            $relative = $Matches[1]
            $rootKind = 'vscode'
            $priority = 2
        } elseif ($full -in @('VERSION.txt', 'README.md', 'LICENCE')) {
            $relative = $full
            $rootKind = 'app'
        } else {
            throw "Unexpected entry in portable archive: $full"
        }

        if ([string]::IsNullOrEmpty($relative)) { continue }

        $root = if ($rootKind -eq 'vscode') { $VsCodeTarget } else { $Target }
        $destination = Assert-SafeDestination $relative $root
        $key = $destination.ToLowerInvariant()

        if ($entry.Length -gt $maxEntryBytes) { throw "Archive entry is too large: $full" }

        $staging = Join-Path (Join-Path $script:StagingRoot $rootKind) $relative.Replace('/', '\')
        $item = @{
            Entry       = $entry
            FullName    = $full
            Relative    = $relative
            Staging     = $staging
            Destination = $destination
            Length      = $entry.Length
            Priority    = $priority
        }

        if ($seen.ContainsKey($key)) {
            $existing = $plan[$seen[$key]]
            if ($priority -gt $existing.Priority) {
                $totalBytes -= $existing.Length
                $plan[$seen[$key]] = $item
            } elseif ($priority -lt $existing.Priority) {
                continue
            } else {
                throw "Duplicate file in archive: $relative"
            }
        } else {
            $seen[$key] = $plan.Count
            $plan.Add($item)
        }

        $totalBytes += $entry.Length
        $compressedBytes += $entry.CompressedLength
        if ($totalBytes -gt $maxTotalBytes) { throw 'Archive expands beyond the allowed total size.' }
    }

    if ($compressedBytes -gt 0 -and $totalBytes -gt 10MB -and ($totalBytes / $compressedBytes) -gt $maxCompressionRatio) {
        throw 'Archive compression ratio is suspicious; refusing to extract.'
    }
    if ($totalBytes -eq 0) { throw 'The portable archive contains no installable files.' }

    return @{ Items = $plan; TotalBytes = $totalBytes }
}

function Test-DiskSpace {
    param([string]$Path, [long]$RequiredBytes)
    $root = [IO.Path]::GetPathRoot($Path)
    $drive = New-Object System.IO.DriveInfo $root
    $need = $RequiredBytes + 64MB
    if ($drive.AvailableFreeSpace -lt $need) {
        throw ("Not enough free disk space on {0}. Need {1:N0} MB, available {2:N0} MB." -f $root, ($need / 1MB), ($drive.AvailableFreeSpace / 1MB))
    }
}

# ---------------------------------------------------------------------------
# Checksum, signature and download helpers
# ---------------------------------------------------------------------------

function Assert-ReleaseUrl {
    param([string]$Url, [string]$AssetName)
    $uri = [Uri]$Url
    if ($uri.Scheme -ne 'https' -or $uri.Host -ne 'github.com') {
        throw "Release asset $AssetName does not point at github.com."
    }
    if (-not $uri.AbsolutePath.StartsWith("/$repo/releases/download/", [StringComparison]::OrdinalIgnoreCase)) {
        throw "Release asset $AssetName points outside the expected release path."
    }
}

function Get-ExpectedHash {
    param([string]$Path)
    $text = [IO.File]::ReadAllText($Path).Trim()
    if ($text -notmatch '^([0-9a-fA-F]{64})\b') { throw 'Checksum file is malformed.' }
    return $Matches[1].ToLowerInvariant()
}

function Test-ArchiveHash {
    param([string]$ArchiveFile, [string]$ChecksumFile)
    $expected = Get-ExpectedHash $ChecksumFile
    $actual = (Get-FileHash -LiteralPath $ArchiveFile -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($expected -ne $actual) { throw 'SHA-256 verification failed; the archive will not be installed.' }
    return $actual
}

function Get-FileHashLower {
    param([string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-ModN {
    param([System.Numerics.BigInteger]$Value, [System.Numerics.BigInteger]$Modulus)
    $result = $Value % $Modulus
    if ($result -lt 0) { $result += $Modulus }
    return $result
}

function Get-IntegerLittleEndian {
    param([byte[]]$Bytes)
    $buffer = New-Object byte[] ($Bytes.Length + 1)
    [Array]::Copy($Bytes, $buffer, $Bytes.Length)
    return [System.Numerics.BigInteger]::new($buffer)
}

function Get-EdwardsPoint {
    param([byte[]]$Encoded)
    if ($Encoded.Length -ne 32) { throw 'Invalid Ed25519 point.' }
    $y = Get-IntegerLittleEndian $Encoded
    $sign = [int](($Encoded[31] -band 0x80) -shr 7)
    $y = $y -band ([System.Numerics.BigInteger]::Pow(2, 255) - 1)
    if ($y -ge $script:CurveQ) { throw 'Invalid Ed25519 point.' }
    $y2 = Get-ModN ($y * $y) $script:CurveQ
    $u = Get-ModN ($y2 - 1) $script:CurveQ
    $v = Get-ModN ($script:CurveD * $y2 + 1) $script:CurveQ
    $x = [System.Numerics.BigInteger]::ModPow((Get-ModN ($u * [System.Numerics.BigInteger]::ModPow($v, $script:CurveQ - 2, $script:CurveQ)) $script:CurveQ), ($script:CurveQ + 3) / 8, $script:CurveQ)
    if ((Get-ModN ($x * $x * $v - $u) $script:CurveQ) -ne 0) { $x = Get-ModN ($x * $script:SqrtM1) $script:CurveQ }
    if ((Get-ModN ($x * $x * $v - $u) $script:CurveQ) -ne 0) { throw 'Invalid Ed25519 point.' }
    if (([int]($x -band 1)) -ne $sign) { $x = $script:CurveQ - $x }
    return @{ X = $x; Y = $y; Z = [System.Numerics.BigInteger]::One; T = (Get-ModN ($x * $y) $script:CurveQ) }
}

function Add-EdwardsPoint {
    param($A, $B)
    $t1 = Get-ModN (($A.Y - $A.X) * ($B.Y - $B.X)) $script:CurveQ
    $t2 = Get-ModN (($A.Y + $A.X) * ($B.Y + $B.X)) $script:CurveQ
    $t3 = Get-ModN ($A.T * 2 * $script:CurveD * $B.T) $script:CurveQ
    $t4 = Get-ModN ($A.Z * 2 * $B.Z) $script:CurveQ
    $e = $t2 - $t1; $f = $t4 - $t3; $g = $t4 + $t3; $h = $t2 + $t1
    return @{
        X = (Get-ModN ($e * $f) $script:CurveQ)
        Y = (Get-ModN ($g * $h) $script:CurveQ)
        Z = (Get-ModN ($f * $g) $script:CurveQ)
        T = (Get-ModN ($e * $h) $script:CurveQ)
    }
}

function Double-EdwardsPoint {
    param($A)
    $t1 = Get-ModN ($A.X * $A.X) $script:CurveQ
    $t2 = Get-ModN ($A.Y * $A.Y) $script:CurveQ
    $t3 = Get-ModN (2 * $A.Z * $A.Z) $script:CurveQ
    $t4 = Get-ModN (-$t1) $script:CurveQ
    $e = Get-ModN ((($A.X + $A.Y) * ($A.X + $A.Y)) - $t1 - $t2) $script:CurveQ
    $g = $t4 + $t2; $f = $g - $t3; $h = $t4 - $t2
    return @{
        X = (Get-ModN ($e * $f) $script:CurveQ)
        Y = (Get-ModN ($g * $h) $script:CurveQ)
        Z = (Get-ModN ($f * $g) $script:CurveQ)
        T = (Get-ModN ($e * $h) $script:CurveQ)
    }
}

function Test-EdwardsPointEqual {
    param($A, $B)
    $x1 = Get-ModN ($A.X * $B.Z) $script:CurveQ
    $x2 = Get-ModN ($B.X * $A.Z) $script:CurveQ
    $y1 = Get-ModN ($A.Y * $B.Z) $script:CurveQ
    $y2 = Get-ModN ($B.Y * $A.Z) $script:CurveQ
    return ($x1 -eq $x2) -and ($y1 -eq $y2)
}

function Test-Ed25519Signature {
    param([byte[]]$PublicKey, [byte[]]$Message, [byte[]]$Signature)
    if ($PublicKey.Length -ne 32 -or $Signature.Length -ne 64) { return $false }
    $rBytes = [byte[]]$Signature[0..31]
    $sBytes = [byte[]]$Signature[32..63]
    $s = Get-IntegerLittleEndian $sBytes
    if ($s -ge $script:OrderL) { return $false }
    try {
        $a = Get-EdwardsPoint $PublicKey
        $r = Get-EdwardsPoint $rBytes
    } catch { return $false }

    $sha = [Security.Cryptography.SHA512]::Create()
    try { $hash = $sha.ComputeHash([byte[]]($rBytes + $PublicKey + $Message)) }
    finally { $sha.Dispose() }
    $k = (Get-IntegerLittleEndian $hash) % $script:OrderL

    $left = @{ X = [System.Numerics.BigInteger]::Zero; Y = [System.Numerics.BigInteger]::One; Z = [System.Numerics.BigInteger]::One; T = [System.Numerics.BigInteger]::Zero }
    $right = @{ X = [System.Numerics.BigInteger]::Zero; Y = [System.Numerics.BigInteger]::One; Z = [System.Numerics.BigInteger]::One; T = [System.Numerics.BigInteger]::Zero }
    $q1 = $script:BasePoint
    $q2 = $a
    for ($i = 0; $i -lt 256; $i++) {
        if ((($s -shr $i) -band 1) -eq 1) { $left = Add-EdwardsPoint $left $q1 }
        if ((($k -shr $i) -band 1) -eq 1) { $right = Add-EdwardsPoint $right $q2 }
        $q1 = Double-EdwardsPoint $q1
        $q2 = Double-EdwardsPoint $q2
    }
    return Test-EdwardsPointEqual $left (Add-EdwardsPoint $r $right)
}

function Test-MinisignSignature {
    param([string]$DataFile, [string]$SignatureFile)
    $lines = @([IO.File]::ReadAllText($SignatureFile) -split "`r?`n" | Where-Object { $_.Length -gt 0 })
    if ($lines.Count -lt 2) { throw 'Signature file is malformed.' }
    $blob = [Convert]::FromBase64String($lines[1].Trim())
    if ($blob.Length -ne 74) { throw 'Signature blob has an invalid length.' }
    $algorithm = [Text.Encoding]::ASCII.GetString($blob[0..1])
    if ($algorithm -ne 'Ed') {
        throw "Unsupported signature algorithm '$algorithm'. Re-sign with the legacy pure Ed25519 format."
    }
    $publicBlob = [Convert]::FromBase64String($minisignPublicKey)
    if ($publicBlob.Length -ne 42) { throw 'Embedded public key is malformed.' }
    if ([Convert]::ToBase64String($blob[2..9]) -cne [Convert]::ToBase64String($publicBlob[2..9])) { throw 'Signature was created by a different key.' }
    $message = [IO.File]::ReadAllBytes($DataFile)
    return (Test-Ed25519Signature $publicBlob[10..41] $message $blob[10..73])
}

function Invoke-Download {
    param([string]$Uri, [string]$OutFile, [string]$Label)
    if (-not $Quiet) { Write-Step "Downloading $Label" }
    $oldProgress = $ProgressPreference
    $ProgressPreference = 'SilentlyContinue'
    try {
        Invoke-WebRequest -Uri $Uri -OutFile $OutFile -UseBasicParsing -TimeoutSec 300
    } finally { $ProgressPreference = $oldProgress }
    if (-not $Quiet) { Write-StepDone "Downloaded $Label" }
}

function Get-Release {
    $url = "https://api.github.com/repos/$repo/releases/latest"
    return Invoke-RestMethod -Uri $url -Headers @{
        'User-Agent' = 'LUC-terminal-installer'
        'Accept'     = 'application/vnd.github+json'
    }
}

# ---------------------------------------------------------------------------
# Lock detection and transactional install
# ---------------------------------------------------------------------------

function Get-AncestorProcessIds {
    $ids = @{}
    $current = $PID
    for ($i = 0; $i -lt 16; $i++) {
        $ids[[int]$current] = $true
        try {
            $proc = Get-CimInstance Win32_Process -Filter "ProcessId=$current" -ErrorAction Stop
            if (-not $proc) { break }
            $next = [int]$proc.ParentProcessId
            if ($next -le 0 -or $ids.ContainsKey($next)) { break }
            $current = $next
        } catch { break }
    }
    return $ids
}

function Test-InstallLocks {
    param([object[]]$Items, [string[]]$Roots)

    $ancestors = Get-AncestorProcessIds
    $ancestorPaths = @{}
    foreach ($id in $ancestors.Keys) {
        try {
            $path = (Get-Process -Id $id -ErrorAction Stop).Path
            if ($path) { $ancestorPaths[$path.ToLowerInvariant()] = $true }
        } catch { }
    }

    $running = @()
    foreach ($proc in (Get-Process -ErrorAction SilentlyContinue)) {
        $path = $null
        try { $path = $proc.Path } catch { continue }
        if (-not $path) { continue }
        if ($ancestors.ContainsKey([int]$proc.Id)) { continue }
        foreach ($root in $Roots) {
            if ($path.StartsWith($root.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
                $running += [pscustomobject]@{ Name = $proc.ProcessName; Id = $proc.Id; Path = $path }
                break
            }
        }
    }
    if ($running.Count -gt 0) {
        $list = ($running | ForEach-Object { "$($_.Name) (PID $($_.Id))" } | Select-Object -Unique) -join ', '
        throw "LUC is still running: $list. Close it and run the installer again."
    }

    $locked = @()
    foreach ($item in $Items) {
        if (-not (Test-Path -LiteralPath $item.Destination -PathType Leaf)) { continue }
        if ($ancestorPaths.ContainsKey($item.Destination.ToLowerInvariant())) { continue }
        try {
            $stream = [IO.File]::Open($item.Destination, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
            $stream.Dispose()
        } catch { $locked += $item.Destination }
    }
    if ($locked.Count -gt 0) {
        $sample = ($locked | Select-Object -First 5) -join "`n    "
        throw "These files are locked by another program:`n    $sample`nClose them and try again."
    }
}

function Expand-ArchivePlan {
    param([object[]]$Items)
    foreach ($item in $Items) {
        $parent = Split-Path -Parent $item.Staging
        if (-not (Test-Path -LiteralPath $parent -PathType Container)) {
            New-Item -ItemType Directory -Force -Path $parent | Out-Null
        }
        $inStream = $item.Entry.Open()
        try {
            $outStream = [IO.File]::Open($item.Staging, [IO.FileMode]::Create, [IO.FileAccess]::Write, [IO.FileShare]::None)
            try { $inStream.CopyTo($outStream, 1048576) }
            finally { $outStream.Dispose() }
        } finally { $inStream.Dispose() }
    }
}

function Remove-FileBestEffort {
    param([string]$Path)
    try {
        Remove-Item -LiteralPath $Path -Force -ErrorAction Stop
        return
    } catch { }
    # A running exe/DLL cannot be deleted, but it can be renamed. Ask Windows
    # to remove the leftover during the next reboot instead of failing.
    try {
        if (-not ('LucNative.ConsoleApi' -as [type])) {
            Add-Type -Namespace LucNative -Name ConsoleApi -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("kernel32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode, SetLastError = true)]
public static extern bool MoveFileEx(string existingFileName, string newFileName, int flags);
'@
        }
        [LucNative.ConsoleApi]::MoveFileEx($Path, $null, 4) | Out-Null
    } catch { }
}

function Install-ArchivePlan {
    param([object[]]$Items)
    $journal = New-Object System.Collections.ArrayList
    try {
        foreach ($item in $Items) {
            $destination = $item.Destination
            $parent = Split-Path -Parent $destination
            if (-not (Test-Path -LiteralPath $parent -PathType Container)) {
                New-Item -ItemType Directory -Force -Path $parent | Out-Null
            }
            $old = $null
            if (Test-Path -LiteralPath $destination -PathType Leaf) {
                $old = "$destination.luc-old-$([guid]::NewGuid().ToString('N').Substring(0, 8))"
                try { Move-Item -LiteralPath $destination -Destination $old -Force }
                catch { throw "Cannot replace '$destination'. Close the program using it and try again." }
            }
            [void]$journal.Add([pscustomobject]@{ Destination = $destination; Old = $old })
            try { Move-Item -LiteralPath $item.Staging -Destination $destination -Force }
            catch { throw "Cannot write '$destination': $($_.Exception.Message)" }
        }
    } catch {
        $failure = $_
        for ($i = $journal.Count - 1; $i -ge 0; $i--) {
            $entry = $journal[$i]
            try {
                if (Test-Path -LiteralPath $entry.Destination -PathType Leaf) {
                    Remove-Item -LiteralPath $entry.Destination -Force -ErrorAction SilentlyContinue
                }
                if ($entry.Old -and (Test-Path -LiteralPath $entry.Old)) {
                    Move-Item -LiteralPath $entry.Old -Destination $entry.Destination -Force
                }
            } catch {
                Write-Warning "Rollback could not restore $($entry.Destination): $($_.Exception.Message)"
            }
        }
        throw $failure
    }

    foreach ($entry in $journal) {
        if ($entry.Old -and (Test-Path -LiteralPath $entry.Old)) {
            Remove-FileBestEffort $entry.Old
        }
    }
}

function Remove-StaleBackups {
    param([string]$Root)
    if (-not (Test-Path -LiteralPath $Root -PathType Container)) { return }
    Get-ChildItem -LiteralPath $Root -File -Filter '*.luc-old-*' -ErrorAction SilentlyContinue |
        Where-Object { $_.LastWriteTime -lt (Get-Date).AddHours(-1) } |
        ForEach-Object { Remove-FileBestEffort $_.FullName }
}

function Add-UserPath {
    param([string]$Folder)
    $existing = [Environment]::GetEnvironmentVariable('Path', 'User')
    $entries = @()
    if (-not [string]::IsNullOrWhiteSpace($existing)) {
        $entries = @($existing -split ';' | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    }
    foreach ($entry in $entries) {
        if ([string]::Equals($entry.TrimEnd([char]92), $Folder.TrimEnd([char]92), [StringComparison]::OrdinalIgnoreCase)) {
            Write-Note 'LUC is already on your user PATH.'
            return
        }
    }
    $entries += $Folder
    [Environment]::SetEnvironmentVariable('Path', ($entries -join ';'), 'User')
    Write-Note 'Added LUC to your user PATH. Open a new terminal to use it.' 'ok'
}

function Write-InstallState {
    param([string]$Version, [string]$Path, [string[]]$Selected, [string]$SourceName)
    $stateDir = Join-Path $env:LOCALAPPDATA 'LUC'
    if (-not (Test-Path -LiteralPath $stateDir -PathType Container)) {
        New-Item -ItemType Directory -Force -Path $stateDir | Out-Null
    }
    $state = [ordered]@{
        version     = $Version
        installPath = $Path
        components  = @('core') + $Selected
        source      = $SourceName
        installedAt = (Get-Date).ToString('o')
    }
    $state | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $stateDir 'state.json') -Encoding UTF8
}

function Write-InstallManifest {
    param([string]$Version, [string]$Path, [string[]]$Selected)
    $manifest = [ordered]@{
        version     = $Version
        components  = @('core') + $Selected
        installedAt = (Get-Date).ToString('o')
    }
    try {
        $manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $Path '.luc-manifest.json') -Encoding UTF8
    } catch {
        Write-Note "Could not write the install manifest: $($_.Exception.Message)" 'warn'
    }
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

$script:CurveQ = [System.Numerics.BigInteger]::Pow(2, 255) - 19
$script:OrderL = [System.Numerics.BigInteger]::Parse('7237005577332262213973186563042994240857116359379907606001950938285454250989')
$script:CurveD = Get-ModN ([System.Numerics.BigInteger](-121665) * ([System.Numerics.BigInteger]::ModPow(121666, $script:CurveQ - 2, $script:CurveQ))) $script:CurveQ
$script:SqrtM1 = [System.Numerics.BigInteger]::ModPow(2, ($script:CurveQ - 1) / 4, $script:CurveQ)
$script:BasePoint = Get-EdwardsPoint ([byte[]](0x58, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66))
$script:StagingRoot = $null

Initialize-Console
$script:Interactive = (-not $Yes) -and (-not $Quiet) -and (-not [Console]::IsInputRedirected)

$downloadTemp = $null
$tempChecksumFile = $null
$zip = $null
try {
    $sourceSpecified = $PSBoundParameters.ContainsKey('Source')
    if (-not $Source) {
        if ($ArchivePath) { $Source = 'local' } else { $Source = 'github' }
    }
    if ($ArchivePath) { $Source = 'local' }

    if ($script:Interactive -and -not $sourceSpecified -and -not $ArchivePath) {
        Enter-AltScreen
        $choice = Show-SourcePage
        Exit-AltScreen
        if ($choice -lt 0) { Write-Note 'Installation cancelled.' 'warn'; exit 2 }
        if ($choice -eq 1) { $Source = 'local' } else { $Source = 'github' }
    }

    if ($Source -eq 'local' -and -not $ArchivePath) {
        if (-not $script:Interactive) { throw 'A local install needs -ArchivePath pointing at luc-portable.zip.' }
        Enter-AltScreen
        $ArchivePath = Show-PathPage -Title 'Portable archive (.zip)' -Default (Join-Path $env:USERPROFILE 'Downloads\luc-portable.zip') -ExistingFile
        Exit-AltScreen
        if (-not $ArchivePath) { Write-Note 'Installation cancelled.' 'warn'; exit 2 }
    }

    $archiveFile = $null
    $checksumFile = $null
    $signatureFile = $null
    $signed = $false

    if ($ArchivePath) {
        if (-not (Test-Path -LiteralPath $ArchivePath -PathType Leaf)) {
            throw "Portable archive not found: $ArchivePath"
        }
        $archiveFile = (Resolve-Path -LiteralPath $ArchivePath).Path
        $checksumFile = "$archiveFile.sha256"
        $signatureFile = "$checksumFile.minisig"
        if (-not (Test-Path -LiteralPath $checksumFile -PathType Leaf)) {
            if ($AllowUnsigned) {
                $tempHash = Join-Path ([IO.Path]::GetTempPath()) ("luc-local-" + [guid]::NewGuid().ToString('N') + '.sha256')
                Set-Content -LiteralPath $tempHash -Value (Get-FileHashLower $archiveFile) -Encoding ASCII
                $checksumFile = $tempHash
                $tempChecksumFile = $tempHash
                Write-Note 'No .sha256 file found; using a locally computed checksum.' 'warn'
            } else {
                throw "Checksum file is missing: $checksumFile"
            }
        }
    } else {
        $release = Get-Release
        if (-not $release.tag_name) { throw 'GitHub returned a release without a version tag.' }
        $script:Version = $release.tag_name -replace '^v', ''

        $assets = @($release.assets)
        $zipAsset = $assets | Where-Object { $_.name -eq 'luc-portable.zip' } | Select-Object -First 1
        $hashAsset = $assets | Where-Object { $_.name -eq 'luc-portable.zip.sha256' } | Select-Object -First 1
        $sigAsset = $assets | Where-Object { $_.name -eq 'luc-portable.zip.sha256.minisig' } | Select-Object -First 1
        if (-not $zipAsset -or -not $hashAsset) {
            throw "Release $($release.tag_name) must include luc-portable.zip and luc-portable.zip.sha256."
        }
        Assert-ReleaseUrl $zipAsset.browser_download_url $zipAsset.name
        Assert-ReleaseUrl $hashAsset.browser_download_url $hashAsset.name
        if ($sigAsset) { Assert-ReleaseUrl $sigAsset.browser_download_url $sigAsset.name }

        $downloadTemp = Join-Path ([IO.Path]::GetTempPath()) ('luc-install-' + [guid]::NewGuid().ToString('N'))
        New-Item -ItemType Directory -Force -Path $downloadTemp | Out-Null
        $archiveFile = Join-Path $downloadTemp 'luc-portable.zip'
        $checksumFile = "$archiveFile.sha256"
        $signatureFile = "$checksumFile.minisig"
        Invoke-Download $zipAsset.browser_download_url $archiveFile 'portable archive'
        Invoke-Download $hashAsset.browser_download_url $checksumFile 'checksum'
        if ($sigAsset) {
            Invoke-Download $sigAsset.browser_download_url $signatureFile 'signature'
        }
    }

    $zip = [IO.Compression.ZipFile]::OpenRead($archiveFile)
    $archiveVersion = (Read-ZipVersion $zip).Trim()
    if (-not $script:Version) { $script:Version = $archiveVersion }
    Show-Banner

    Write-Step 'Verifying SHA-256'
    $actualHash = Test-ArchiveHash $archiveFile $checksumFile
    Write-StepDone 'SHA-256 verified'

    if (Test-Path -LiteralPath $signatureFile -PathType Leaf) {
        Write-Step 'Verifying release signature'
        if (-not (Test-MinisignSignature $checksumFile $signatureFile)) {
            throw 'Release signature is invalid; the archive will not be installed.'
        }
        $signed = $true
        Write-StepDone 'Release signature verified'
    } elseif ($minisignPublicKey) {
        if ($AllowUnsigned -or $Source -eq 'github') {
            Write-Note 'This release is not signed; SHA-256 is the only integrity check.' 'warn'
        } else {
            throw 'The checksum signature is missing. Use a signed release, or pass -AllowUnsigned explicitly.'
        }
    }

    if ($Source -eq 'github' -and $zipAsset -and $zipAsset.digest) {
        $expectedDigest = [string]$zipAsset.digest
        if ($expectedDigest -ne "sha256:$actualHash") {
            throw 'The GitHub asset digest does not match the downloaded archive.'
        }
    }

    if ($Source -eq 'github' -and $archiveVersion -ne $script:Version) {
        throw 'The downloaded archive version does not match its GitHub release tag.'
    }
    $script:Version = $archiveVersion
    if (-not $Quiet) { Write-Note "LUC $($script:Version) verified" 'ok' }

    $licenseText = Read-ZipEntryText $zip 'LICENCE'
    if (-not $licenseText) { $licenseText = 'LUC is free software released under the Apache License 2.0.' }

    $selected = @()
    if ($Components) {
        foreach ($item in $Components) {
            foreach ($name in ($item -split '[,\s]+')) {
                if ([string]::IsNullOrWhiteSpace($name)) { continue }
                $name = $name.ToLowerInvariant()
                if ($name -eq 'core') { continue }
                if ($optionalComponents -notcontains $name) { throw "Unknown component: $name" }
                if ($selected -notcontains $name) { $selected += $name }
            }
        }
    }

    if ($script:Interactive -and -not $Components) {
        Enter-AltScreen
        $picked = Show-ComponentPage -Preselected $selected -LicenseText $licenseText
        Exit-AltScreen
        if ($null -eq $picked) { Write-Note 'Installation cancelled.' 'warn'; exit 2 }
        $selected = @($picked)
    }

    if (-not $InstallPath) {
        if ($script:Interactive) {
            Enter-AltScreen
            $InstallPath = Show-PathPage -Title 'Install folder' -Default $defaultInstallPath
            Exit-AltScreen
            if (-not $InstallPath) { Write-Note 'Installation cancelled.' 'warn'; exit 2 }
        } else {
            $InstallPath = Resolve-InstallPathValue $defaultInstallPath
        }
    } else {
        $InstallPath = Resolve-InstallPathValue $InstallPath
    }

    $vsCodeTarget = Join-Path $env:USERPROFILE '.vscode\extensions\hsusulist.luc'
    $upgrade = Test-Path -LiteralPath (Join-Path $InstallPath 'luc.exe') -PathType Leaf
    if ($upgrade -and -not $Force -and -not $Yes -and -not $script:Interactive) {
        throw "LUC is already installed in '$InstallPath'. Pass -Force to replace it."
    }

    if ($script:Interactive) {
        Enter-AltScreen
        Show-LicensePage $licenseText
        $summary = Show-SummaryPage ([pscustomobject]@{
            Version     = $script:Version
            Source      = $Source
            InstallPath = $InstallPath
            Components  = $selected
            Upgrade     = $upgrade
        })
        Exit-AltScreen
        if ($summary -eq 'back') {
            Enter-AltScreen
            $picked = Show-ComponentPage -Preselected $selected -LicenseText $licenseText
            $InstallPath = Show-PathPage -Title 'Install folder' -Default $InstallPath
            Exit-AltScreen
            if ($null -eq $picked -or -not $InstallPath) { Write-Note 'Installation cancelled.' 'warn'; exit 2 }
            $selected = @($picked)
        }
    }

    $script:StagingRoot = Join-Path (Split-Path -Parent $InstallPath) ('.luc-staging-' + [guid]::NewGuid().ToString('N'))
    $archivePlan = Get-ArchivePlan -Zip $zip -SelectedComponents $selected -Target $InstallPath -VsCodeTarget $vsCodeTarget
    if (-not ($archivePlan.Items | Where-Object { $_.Relative -eq 'luc.exe' -and $_.Destination -eq (Join-Path $InstallPath 'luc.exe') })) {
        throw 'Core installation did not produce luc.exe.'
    }

    $roots = @($InstallPath)
    if ($selected -contains 'vscode') { $roots += $vsCodeTarget }
    Write-Step 'Checking for running programs and locked files'
    Test-InstallLocks -Items $archivePlan.Items -Roots $roots
    Write-StepDone 'No running LUC process blocks the install'

    Test-DiskSpace $InstallPath $archivePlan.TotalBytes

    Write-Step 'Extracting to a staging folder'
    if (-not (Test-Path -LiteralPath $script:StagingRoot -PathType Container)) {
        New-Item -ItemType Directory -Force -Path $script:StagingRoot | Out-Null
    }
    Expand-ArchivePlan $archivePlan.Items
    Write-StepDone "Extracted $($archivePlan.Items.Count) files"

    Write-Step 'Installing files'
    Install-ArchivePlan $archivePlan.Items
    Write-StepDone 'Files installed'

    if (-not (Test-Path -LiteralPath (Join-Path $InstallPath 'luc.exe') -PathType Leaf)) {
        throw 'Installation finished without luc.exe. The previous version was restored.'
    }

    Write-InstallManifest $script:Version $InstallPath $selected
    Write-InstallState $script:Version $InstallPath $selected $Source

    # Keep a copy of this script next to luc.exe so that "luc --install" works
    # even when the archive was built before install.ps1 was bundled in it.
    try {
        $selfScript = $PSCommandPath
        if (-not $selfScript) { $selfScript = $MyInvocation.MyCommand.Path }
        $targetScript = Join-Path $InstallPath 'install.ps1'
        if ($selfScript -and (Test-Path -LiteralPath $selfScript -PathType Leaf) -and
            -not [string]::Equals($selfScript, $targetScript, [StringComparison]::OrdinalIgnoreCase)) {
            Copy-Item -LiteralPath $selfScript -Destination $targetScript -Force
        }
    } catch {
        Write-Note "Could not copy install.ps1 next to luc.exe: $($_.Exception.Message)" 'warn'
    }

    Remove-StaleBackups $InstallPath
    if ($selected -contains 'vscode') { Remove-StaleBackups $vsCodeTarget }

    if (-not $NoEnvironmentChanges) {
        [Environment]::SetEnvironmentVariable('LUC_PATH', (Join-Path $InstallPath 'luc_modules'), 'User')
        if (-not $NoPath) {
            $addPath = $true
            if ($script:Interactive -and -not $Yes) {
                Write-Host ''
                $answer = Read-Host 'Add LUC to your user PATH? [Y/n]'
                if ($answer -match '^(n|no)$') { $addPath = $false }
            }
            if ($addPath) { Add-UserPath $InstallPath }
        }
    }

    Write-Host ''
    Write-Host "LUC $($script:Version) installed to $InstallPath" -ForegroundColor Green
    Write-Host 'Open a new terminal, then run: luc --help'
    exit 0
} catch {
    if ($script:InAltScreen) { Exit-AltScreen }
    Write-Note $_.Exception.Message 'err'
    if ($env:LUC_INSTALL_DEBUG) { Write-Host $_.ScriptStackTrace }
    exit 1
} finally {
    if ($zip) { try { $zip.Dispose() } catch { } }
    if ($script:StagingRoot -and (Test-Path -LiteralPath $script:StagingRoot -PathType Container)) {
        try { Remove-Item -LiteralPath $script:StagingRoot -Recurse -Force } catch { }
    }
    if ($downloadTemp -and (Test-Path -LiteralPath $downloadTemp -PathType Container)) {
        try { Remove-Item -LiteralPath $downloadTemp -Recurse -Force } catch { }
    }
    if ($tempChecksumFile -and (Test-Path -LiteralPath $tempChecksumFile -PathType Leaf)) {
        try { Remove-Item -LiteralPath $tempChecksumFile -Force } catch { }
    }
    if ($script:InAltScreen) { Exit-AltScreen }
    try { [Console]::CursorVisible = $true } catch { }
}
