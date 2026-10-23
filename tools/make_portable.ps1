$ErrorActionPreference = 'Stop'

$ProjectDir = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$AppDir = Join-Path $ProjectDir 'dist\app'
$PortableDir = Join-Path $ProjectDir 'dist'
$ZipPath = Join-Path $PortableDir 'luc-portable.zip'
$StageDir = Join-Path ([IO.Path]::GetTempPath()) ('luc-portable-' + [guid]::NewGuid().ToString('N'))
$header = Get-Content -LiteralPath (Join-Path $ProjectDir 'src\luc.h') -Raw
if ($header -notmatch '#define\s+LUC_VERSION\s+"LUC\s+([^"]+)"') {
    throw 'Could not read LUC_VERSION from src\luc.h.'
}
$Version = $Matches[1]
$installer = Get-Content -LiteralPath (Join-Path $ProjectDir 'installer\luc-installer.iss') -Raw
if ($installer -notmatch '(?m)^AppVersion=([^\r\n]+)' -or $Matches[1] -ne $Version) {
    throw 'The installer AppVersion does not match LUC_VERSION in src\luc.h.'
}

$windowFiles = @(
    'SDL2.dll', 'SDL2_ttf.dll', 'SDL2_image.dll', 'SDL2_mixer.dll',
    'libfreetype-6.dll', 'libharfbuzz-0.dll', 'libbz2-1.dll',
    'libpng16-16.dll', 'zlib1.dll', 'libbrotlidec.dll', 'libbrotlicommon.dll',
    'libgraphite2.dll', 'libglib-2.0-0.dll', 'libintl-8.dll',
    'libpcre2-8-0.dll', 'libiconv-2.dll', 'libjpeg-8.dll',
    'libmpg123-0.dll', 'libogg-0.dll', 'libopus-0.dll', 'libopusfile-0.dll',
    'libvorbis-0.dll', 'libvorbisfile-3.dll', 'libFLAC.dll',
    'libgcc_s_seh-1.dll', 'libstdc++-6.dll', 'libwinpthread-1.dll',
    'DejaVuSans.ttf'
)

function Copy-DirectoryContents {
    param([string]$Source, [string]$Destination)
    if (-not (Test-Path -LiteralPath $Source -PathType Container)) {
        throw "Required folder is missing: $Source"
    }
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    Get-ChildItem -LiteralPath $Source -Force | Copy-Item -Destination $Destination -Recurse -Force
}

function Copy-RequiredFile {
    param([string]$Source, [string]$Destination)
    if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) {
        throw "Required file is missing: $Source"
    }
    $parent = Split-Path -Parent $Destination
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
    Copy-Item -LiteralPath $Source -Destination $Destination -Force
}

try {
    New-Item -ItemType Directory -Force -Path $PortableDir | Out-Null
    New-Item -ItemType Directory -Force -Path $StageDir | Out-Null
    foreach ($exe in @('luc-core.exe', 'luc-win.exe')) {
        $exePath = Join-Path $AppDir $exe
        if (-not (Test-Path -LiteralPath $exePath -PathType Leaf)) {
            throw "Required executable is missing: $exePath"
        }
        $exeVersion = (& $exePath --version 2>&1 | Out-String)
        if ($LASTEXITCODE -ne 0 -or $exeVersion -notmatch [regex]::Escape("LUC $Version")) {
            throw "$exe does not report the source version LUC $Version."
        }
    }
    Set-Content -LiteralPath (Join-Path $StageDir 'VERSION.txt') -Value $Version -Encoding ASCII
    Copy-RequiredFile (Join-Path $ProjectDir 'README.md') (Join-Path $StageDir 'README.md')
    Copy-RequiredFile (Join-Path $ProjectDir 'LICENCE') (Join-Path $StageDir 'LICENCE')
    Copy-RequiredFile (Join-Path $ProjectDir 'install.ps1') (Join-Path $StageDir 'core\install.ps1')
    Copy-RequiredFile (Join-Path $ProjectDir 'installer\minisign.pub') (Join-Path $StageDir 'core\minisign.pub')
    Copy-RequiredFile (Join-Path $AppDir 'luc-core.exe') (Join-Path $StageDir 'core\luc.exe')
    Copy-RequiredFile (Join-Path $AppDir 'luc-win.exe') (Join-Path $StageDir 'window\luc.exe')

    foreach ($name in $windowFiles) {
        Copy-RequiredFile (Join-Path $AppDir $name) (Join-Path $StageDir "window\$name")
        Copy-RequiredFile (Join-Path $AppDir $name) (Join-Path $StageDir "window\packages\window\$name")
    }

    Copy-DirectoryContents (Join-Path $ProjectDir 'demos') (Join-Path $StageDir 'common\demos')
    Copy-DirectoryContents (Join-Path $ProjectDir 'plugin') (Join-Path $StageDir 'lcode\plugin')
    Copy-DirectoryContents (Join-Path $ProjectDir 'vscode') (Join-Path $StageDir 'vscode')
    Copy-RequiredFile (Join-Path $ProjectDir 'packages\ai.lucpkg') (Join-Path $StageDir 'ai\packages\ai.lucpkg')
    Copy-RequiredFile (Join-Path $ProjectDir 'packages\discord.lucpkg') (Join-Path $StageDir 'discord\packages\discord.lucpkg')
    Copy-RequiredFile (Join-Path $ProjectDir 'luc_modules\discord.luc') (Join-Path $StageDir 'discord\luc_modules\discord.luc')

    $aiModules = Join-Path $StageDir 'ai\luc_modules'
    New-Item -ItemType Directory -Force -Path $aiModules | Out-Null
    Get-ChildItem -LiteralPath (Join-Path $ProjectDir 'luc_modules') -File |
        Where-Object { $_.Name -ne 'discord.luc' } |
        Copy-Item -Destination $aiModules -Force

    $font = Join-Path $ProjectDir 'fonts\FiraCodeNerdFontMono-Regular.ttf'
    if (Test-Path -LiteralPath $font -PathType Leaf) {
        Copy-RequiredFile $font (Join-Path $StageDir 'lcode\fonts\FiraCodeNerdFontMono-Regular.ttf')
    }

    $sourceDir = Join-Path $StageDir 'source\src'
    New-Item -ItemType Directory -Force -Path $sourceDir | Out-Null
    Get-ChildItem -LiteralPath (Join-Path $ProjectDir 'src') -File |
        Where-Object { $_.Extension -eq '.c' -or $_.Extension -eq '.h' } |
        Copy-Item -Destination $sourceDir -Force

    if (Test-Path -LiteralPath $ZipPath) {
        Remove-Item -LiteralPath $ZipPath -Force
    }
    Compress-Archive -Path (Join-Path $StageDir '*') -DestinationPath $ZipPath -CompressionLevel Optimal
    $hash = (Get-FileHash -LiteralPath $ZipPath -Algorithm SHA256).Hash.ToLowerInvariant()
    Set-Content -LiteralPath "$ZipPath.sha256" -Value $hash -Encoding ASCII

    $installerText = Get-Content -LiteralPath (Join-Path $ProjectDir 'install.ps1') -Raw
    if ($installerText -notmatch "(?m)\`$minisignPublicKey\s*=\s*'([^']+)'") {
        throw 'install.ps1 does not declare a minisign public key.'
    }
    $installerKey = $Matches[1]
    $publicFile = Join-Path $ProjectDir 'installer\minisign.pub'
    $publicLines = @(Get-Content -LiteralPath $publicFile | Where-Object { $_.Trim() })
    if ($publicLines.Count -lt 2) { throw 'installer\minisign.pub is malformed.' }
    if ($publicLines[1].Trim() -ne $installerKey) {
        throw 'install.ps1 embeds a different public key than installer\minisign.pub.'
    }

    $signingKey = $env:LUC_SIGNING_KEY
    if (-not $signingKey) { $signingKey = Join-Path $env:USERPROFILE '.luc\keys\luc-release.key' }
    $node = Get-Command node -ErrorAction SilentlyContinue
    if ((Test-Path -LiteralPath $signingKey -PathType Leaf) -and $node) {
        & $node.Source (Join-Path $ProjectDir 'tools\lucsig.js') sign "$ZipPath.sha256" --key $signingKey
        if ($LASTEXITCODE -ne 0) { throw 'Signing the release checksum failed.' }
    } elseif (Test-Path -LiteralPath $signingKey -PathType Leaf) {
        Write-Warning 'node was not found; the checksum was not signed.'
    } else {
        Write-Warning "Signing key not found at $signingKey; the checksum was not signed."
    }

    Write-Host "Portable release created: $ZipPath"
    Write-Host "SHA-256: $hash"
} finally {
    if (Test-Path -LiteralPath $StageDir -PathType Container) {
        Remove-Item -LiteralPath $StageDir -Recurse -Force
    }
}
