param(
    [string]$InstallPath,
    [string[]]$Components,
    [string]$ArchivePath,
    [switch]$NoPath,
    [switch]$NoEnvironmentChanges,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$repo = 'hsusulist/luc'
$componentNames = @('core', 'window', 'ai', 'discord', 'lcode', 'vscode', 'source')
$defaultInstallPath = Join-Path $env:LOCALAPPDATA 'Programs\LUC'

function Show-LucBanner {
    param([string]$Version)
    $displayVersion = $Version -replace '-beta([0-9]+)$', ' beta $1' -replace '-rc([0-9]+)$', ' rc $1'
    [Console]::OutputEncoding = [Text.UTF8Encoding]::new()
    Write-Host ''
    Write-Host "██╗     ██╗   ██╗ ██████╗  Version: $displayVersion" -ForegroundColor Cyan
    Write-Host '██║     ██║   ██║██╔════╝  License: Apache' -ForegroundColor Cyan
    Write-Host '██║     ██║   ██║██║       Repo: https://github.com/hsusulist/luc' -ForegroundColor Cyan
    Write-Host '██║     ██║   ██║██║       Welcome to luc.' -ForegroundColor Cyan
    Write-Host '██████╗╚██████╔╝╚██████╗   Type "help", "credits" or "license" for more information.' -ForegroundColor Cyan
    Write-Host '╚══════╝ ╚═════╝  ╚═════╝' -ForegroundColor Cyan
    Write-Host ''
}

function Read-Release {
    $url = "https://api.github.com/repos/$repo/releases/latest"
    Invoke-RestMethod -Uri $url -Headers @{
        'User-Agent' = 'LUC-terminal-installer'
        'Accept' = 'application/vnd.github+json'
    }
}

function Read-ZipVersion {
    param([string]$Path)
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [IO.Compression.ZipFile]::OpenRead($Path)
    try {
        $entry = $archive.GetEntry('VERSION.txt')
        if (-not $entry) { throw 'Portable archive has no VERSION.txt.' }
        $reader = [IO.StreamReader]::new($entry.Open())
        try { return $reader.ReadToEnd().Trim() }
        finally { $reader.Dispose() }
    } finally {
        $archive.Dispose()
    }
}

function Select-Components {
    Write-Host 'Core interpreter is required.'
    Write-Host 'Optional components:'
    Write-Host '  1. Window support (SDL2 graphics, sound, and Pong demo)'
    Write-Host '  2. AI libraries'
    Write-Host '  3. Discord library'
    Write-Host '  4. LCode editor plugin support'
    Write-Host '  5. VS Code extension'
    Write-Host '  6. C source files'
    Write-Host 'Enter numbers separated by commas, or leave blank for core only.'
    $choices = @{
        '1' = 'window'; '2' = 'ai'; '3' = 'discord'
        '4' = 'lcode'; '5' = 'vscode'; '6' = 'source'
    }
    while ($true) {
        $answer = Read-Host 'Components'
        if ([string]::IsNullOrWhiteSpace($answer)) { return @() }
        $selected = @()
        $invalid = $false
        foreach ($item in ($answer -split '[,\s]+')) {
            if (-not $choices.ContainsKey($item)) {
                Write-Host "Invalid choice: $item" -ForegroundColor Yellow
                $invalid = $true
                break
            }
            if ($selected -notcontains $choices[$item]) { $selected += $choices[$item] }
        }
        if (-not $invalid) { return $selected }
    }
}

function Resolve-InstallPath {
    param([string]$RequestedPath)
    if ([string]::IsNullOrWhiteSpace($RequestedPath)) {
        $answer = Read-Host "Install folder [$defaultInstallPath]"
        if ([string]::IsNullOrWhiteSpace($answer)) { $RequestedPath = $defaultInstallPath }
        else { $RequestedPath = $answer }
    }
    $full = [IO.Path]::GetFullPath($RequestedPath)
    $root = [IO.Path]::GetPathRoot($full)
    if ($full.TrimEnd([char]92) -eq $root.TrimEnd([char]92)) {
        throw 'Choose a folder below a drive root, not the drive root itself.'
    }
    return $full.TrimEnd([char]92)
}

function Assert-SafeArchiveEntry {
    param([string]$RelativePath, [string]$Root)
    $relative = $RelativePath.Replace('\', '/')
    if ($relative.StartsWith('/') -or $relative -match '^[A-Za-z]:' -or
        ($relative -split '/') -contains '..') {
        throw "Unsafe path in portable archive: $RelativePath"
    }
    $destination = [IO.Path]::GetFullPath((Join-Path $Root ($relative.Replace('/', '\'))))
    $prefix = $Root.TrimEnd('\') + '\'
    if (-not $destination.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Archive entry escapes the install folder: $relative"
    }
    return $destination
}

function Copy-ZipPrefix {
    param(
        [IO.Compression.ZipArchive]$Archive,
        [string]$Prefix,
        [string]$DestinationRoot
    )
    foreach ($entry in $Archive.Entries) {
        $normalized = $entry.FullName.Replace('\', '/')
        if (-not $normalized.StartsWith($Prefix, [StringComparison]::OrdinalIgnoreCase)) { continue }
        $relative = $normalized.Substring($Prefix.Length)
        if ([string]::IsNullOrEmpty($relative)) { continue }
        $mode = ($entry.ExternalAttributes -shr 16) -band 0xF000
        if ($mode -eq 0xA000) { throw "Symbolic links are not allowed in the archive: $relative" }
        $destination = Assert-SafeArchiveEntry $relative $DestinationRoot
        if ($normalized.EndsWith('/')) {
            New-Item -ItemType Directory -Force -Path $destination | Out-Null
            continue
        }
        if ($entry.Length -gt 1073741824) { throw "Archive entry is too large: $normalized" }
        $parent = Split-Path -Parent $destination
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
        $input = $entry.Open()
        try {
            $output = [IO.File]::Open($destination, [IO.FileMode]::Create, [IO.FileAccess]::Write)
            try { $input.CopyTo($output) }
            finally { $output.Dispose() }
        } finally { $input.Dispose() }
    }
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
            Write-Host 'LUC is already on your user PATH.'
            return
        }
    }
    $entries += $Folder
    [Environment]::SetEnvironmentVariable('Path', ($entries -join ';'), 'User')
    Write-Host 'Added LUC to your user PATH. Open a new terminal to use it.'
}

$release = $null
$tempDir = $null
$archiveFile = $ArchivePath
$version = '0.2-beta1'
if ($ArchivePath) {
    if (-not (Test-Path -LiteralPath $ArchivePath -PathType Leaf)) {
        throw "Portable archive not found: $ArchivePath"
    }
    $archiveFile = (Resolve-Path -LiteralPath $ArchivePath).Path
    $version = Read-ZipVersion $archiveFile
} else {
    $release = Read-Release
    if (-not $release.tag_name) { throw 'GitHub returned a release without a version tag.' }
    $version = $release.tag_name -replace '^v', ''
}

Show-LucBanner $version

if (-not $Components) { $Components = Select-Components }
$Components = @($Components | ForEach-Object { $_ -split '[,\s]+' } | Where-Object { $_ })
foreach ($component in $Components) {
    if ($componentNames -notcontains $component) { throw "Unknown component: $component" }
}
$InstallPath = Resolve-InstallPath $InstallPath
if ((Test-Path -LiteralPath (Join-Path $InstallPath 'luc.exe')) -and -not $Force) {
    $answer = Read-Host "LUC already exists in '$InstallPath'. Overwrite selected files? [y/N]"
    if ($answer -notmatch '^(y|yes)$') { Write-Host 'Installation cancelled.'; exit 0 }
}

try {
    if (-not $archiveFile) {
        $assets = @($release.assets)
        $zipAsset = $assets | Where-Object { $_.name -eq 'luc-portable.zip' } | Select-Object -First 1
        $hashAsset = $assets | Where-Object { $_.name -eq 'luc-portable.zip.sha256' } | Select-Object -First 1
        if (-not $zipAsset -or -not $hashAsset) {
            throw "Release $($release.tag_name) must include luc-portable.zip and luc-portable.zip.sha256."
        }
        $tempDir = Join-Path ([IO.Path]::GetTempPath()) ('luc-install-' + [guid]::NewGuid().ToString('N'))
        New-Item -ItemType Directory -Force -Path $tempDir | Out-Null
        $archiveFile = Join-Path $tempDir 'luc-portable.zip'
        $hashFile = "$archiveFile.sha256"
        Write-Host "Downloading LUC $version..."
        Invoke-WebRequest -Uri $zipAsset.browser_download_url -OutFile $archiveFile -UseBasicParsing
        Invoke-WebRequest -Uri $hashAsset.browser_download_url -OutFile $hashFile -UseBasicParsing
        $expectedHash = (Get-Content -LiteralPath $hashFile -Raw).Trim().Split(' ')[0].ToLowerInvariant()
        $actualHash = (Get-FileHash -LiteralPath $archiveFile -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($expectedHash -notmatch '^[0-9a-f]{64}$' -or $expectedHash -ne $actualHash) {
            throw 'SHA-256 verification failed; the archive will not be installed.'
        }
        if ((Read-ZipVersion $archiveFile) -ne $version) {
            throw 'The downloaded archive version does not match its GitHub release tag.'
        }
    } else {
        $hashFile = "$archiveFile.sha256"
        if (-not (Test-Path -LiteralPath $hashFile -PathType Leaf)) {
            throw "Checksum file is missing: $hashFile"
        }
        $expectedHash = (Get-Content -LiteralPath $hashFile -Raw).Trim().Split(' ')[0].ToLowerInvariant()
        $actualHash = (Get-FileHash -LiteralPath $archiveFile -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($expectedHash -notmatch '^[0-9a-f]{64}$' -or $expectedHash -ne $actualHash) {
            throw 'SHA-256 verification failed; the archive will not be installed.'
        }
    }

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [IO.Compression.ZipFile]::OpenRead($archiveFile)
    try {
        $archiveVersion = Read-ZipVersion $archiveFile
        if ($archiveVersion -ne $version) { throw 'Portable archive version changed during setup.' }
        New-Item -ItemType Directory -Force -Path $InstallPath | Out-Null
        Copy-ZipPrefix $zip 'core/' $InstallPath
        Copy-ZipPrefix $zip 'common/' $InstallPath
        foreach ($component in $Components) {
            if ($component -eq 'vscode') {
                $extensionPath = Join-Path $env:USERPROFILE '.vscode\extensions\hsusulist.luc'
                Copy-ZipPrefix $zip 'vscode/' $extensionPath
            } else {
                Copy-ZipPrefix $zip "$component/" $InstallPath
            }
        }
    } finally {
        $zip.Dispose()
    }

    if (-not (Test-Path -LiteralPath (Join-Path $InstallPath 'luc.exe') -PathType Leaf)) {
        throw 'Core installation did not produce luc.exe.'
    }
    if (-not $NoEnvironmentChanges) {
        [Environment]::SetEnvironmentVariable('LUC_PATH', (Join-Path $InstallPath 'luc_modules'), 'User')
        if (-not $NoPath) {
            $answer = Read-Host 'Add LUC to your user PATH? [Y/n]'
            if ($answer -notmatch '^(n|no)$') { Add-UserPath $InstallPath }
        }
    }
    Write-Host ''
    Write-Host "LUC $version installed to $InstallPath" -ForegroundColor Green
    Write-Host 'Open a new terminal, then run: luc --help'
} finally {
    if ($tempDir -and (Test-Path -LiteralPath $tempDir -PathType Container)) {
        Remove-Item -LiteralPath $tempDir -Recurse -Force
    }
}
