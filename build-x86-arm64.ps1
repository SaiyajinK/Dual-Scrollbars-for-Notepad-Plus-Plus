# Manual build and release preparation for the missing Notepad++ architectures.
# Run on Windows with Visual Studio 2026 (MSVC v145, Windows SDK, ARM64 tools).
$ErrorActionPreference = 'Stop'

$root = $PSScriptRoot
$project = Join-Path $root 'DualScrollbars.vcxproj'
$version = '1.1.0'     # Keep in sync with src/DualScrollbarsVersion.rc.
$tag = '1.1.0'         # Existing GitHub release tag (without 'v').
$repo = 'https://github.com/SaiyajinK/Dual-Scrollbars-for-Notepad-Plus-Plus'
$dist = Join-Path $root 'dist'
New-Item -ItemType Directory -Force -Path $dist | Out-Null

$msbuildCommand = Get-Command 'MSBuild.exe' -ErrorAction SilentlyContinue
if ($msbuildCommand) {
    $msbuild = $msbuildCommand.Source
} else {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) {
        throw 'Visual Studio 2026 / MSBuild not found. Check your VS 2026 C++ workload.'
    }
    $vsPath = (& $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -property installationPath | Select-Object -First 1)
    if (-not $vsPath) { throw 'Could not locate a Visual Studio installation with MSBuild.' }
    $msbuild = Join-Path $vsPath 'MSBuild\Current\Bin\MSBuild.exe'
    if (-not (Test-Path $msbuild)) { throw "MSBuild.exe not found: $msbuild" }
}

$architectures = @(
    @{ Platform = 'Win32'; PackageArch = 'x86' },
    @{ Platform = 'ARM64'; PackageArch = 'arm64' }
)

foreach ($architecture in $architectures) {
    $platform = $architecture.Platform
    $arch = $architecture.PackageArch
    Write-Host "Building $arch (Visual Studio platform: $platform)..."
    & $msbuild $project /m /p:Configuration=Release "/p:Platform=$platform" /nologo
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed for $arch. Check that the VS 2026 toolchain for $platform is installed."
    }

    $dll = Join-Path $root "bin\$platform\Release\DualScrollbars.dll"
    if (-not (Test-Path $dll)) { throw "Expected DLL missing: $dll" }

    $zipName = "Dual-Scrollbars-v$version-$arch.zip"
    $zip = Join-Path $dist $zipName
    if (Test-Path $zip) { Remove-Item -LiteralPath $zip -Force }
    # DLL at archive root: mandatory for Notepad++ Plugins Admin.
    Compress-Archive -LiteralPath $dll -DestinationPath $zip -CompressionLevel Optimal
    $sha256 = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()

    $description = 'Adds a second vertical scrollbar with mirrored line numbers and markers in Notepad++.'
    $entry = [ordered]@{
        'folder-name' = 'DualScrollbars'
        'display-name' = 'Dual Scrollbars'
        'version' = $version
        'id' = $sha256
        'repository' = "$repo/releases/download/$tag/$zipName"
        'description' = $description
        'author' = 'SaiyajinK'
        'homepage' = $repo
    }
    $jsonFile = Join-Path $dist "nppPluginList-$arch-entry.json"
    $entry | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath $jsonFile -Encoding UTF8

    Write-Host "Created: $zip"
    Write-Host "SHA-256: $sha256"
    Write-Host "Plugin list entry: $jsonFile"
}

Write-Host 'Done. Test the x86 and ARM64 DLLs before publishing and updating nppPluginList.'
