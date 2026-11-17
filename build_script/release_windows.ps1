# Build the Windows Release application and publish its runtime package.
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$buildDirectory = Join-Path $projectRoot 'build/windows/release'
$stagingDirectory = Join-Path $buildDirectory 'package'
$outputDirectory = Join-Path $projectRoot 'Output/windows'

function Remove-PackageDirectory([string]$Path) {
    $resolved = [IO.Path]::GetFullPath($Path)
    if ($resolved -ne $stagingDirectory -and $resolved -ne $outputDirectory) {
        throw "Refusing to remove unexpected directory: $resolved"
    }
    # Do not follow junctions or symbolic links while replacing generated output.
    $ancestor = $resolved
    while ($ancestor -ne $projectRoot) {
        if (-not $ancestor.StartsWith($projectRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Package directory is outside the project: $resolved"
        }
        if ((Test-Path -LiteralPath $ancestor) -and
            ((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw "Package directory must not traverse a link: $ancestor"
        }
        $ancestor = Split-Path -Parent $ancestor
    }
    if (-not (Test-Path -LiteralPath $resolved)) { return }
    $links = @(Get-ChildItem -LiteralPath $resolved -Force -Recurse |
        Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint })
    if ($links.Count) { throw "Package contains links; review it manually: $resolved" }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}

$enteredProject = $false
try {
    if ($env:OS -ne 'Windows_NT' -or -not [Environment]::Is64BitProcess) {
        throw 'Run this script in 64-bit Windows PowerShell.'
    }
    $cmake = (Get-Command cmake.exe -ErrorAction Stop).Source
    Push-Location -LiteralPath $projectRoot
    $enteredProject = $true

    & $cmake --preset windows-msvc-release
    if ($LASTEXITCODE -ne 0) { throw 'Release configure failed. Prepare dependencies with bootstrap.ps1 first.' }
    & $cmake --build --preset windows-msvc-release --target sdl_window_test --parallel
    if ($LASTEXITCODE -ne 0) { throw 'Release build failed; existing Output/windows was retained.' }

    Remove-PackageDirectory $stagingDirectory
    & $cmake --install $buildDirectory --config Release --component Runtime --prefix $stagingDirectory
    if ($LASTEXITCODE -ne 0) { throw 'Release packaging failed; existing Output/windows was retained.' }
    foreach ($relative in @('sdl_window_test.exe', 'SDL3.dll', 'libmpv-2.dll', 'resources')) {
        if (-not (Test-Path -LiteralPath (Join-Path $stagingDirectory $relative))) {
            throw "Incomplete package: $relative. Existing Output/windows was retained."
        }
    }

    New-Item -ItemType Directory -Path (Join-Path $projectRoot 'Output') -Force | Out-Null
    Remove-PackageDirectory $outputDirectory
    Move-Item -LiteralPath $stagingDirectory -Destination $outputDirectory
    Write-Host "PASS: Release package created at $outputDirectory" -ForegroundColor Green
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 1
} finally {
    if ($enteredProject) { Pop-Location }
}
