# 설치된 도구로 Windows SDL3/mpv SDK를 준비합니다. 도구 버전 차이로 중단하지 않습니다.
[CmdletBinding()]
param(
    [string]$MsysRoot = 'C:\msys64',
    [switch]$CheckOnly
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$previousEncoding = [Console]::OutputEncoding
$previousPythonEncoding = $env:PYTHONIOENCODING

try {
    [Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false)
    $env:PYTHONIOENCODING = 'utf-8'
    Write-Host '[시작] Windows SDL3/mpv SDK bootstrap'
    Write-Host '설치된 도구를 사용합니다. 자동 설치/업데이트는 하지 않습니다.'
    if ($env:OS -ne 'Windows_NT' -or -not [Environment]::Is64BitProcess) {
        throw '64비트 Windows PowerShell에서 실행하세요.'
    }
    $python = Join-Path $MsysRoot 'mingw64/bin/python.exe'
    if (-not (Test-Path -LiteralPath $python -PathType Leaf)) {
        throw "MINGW64 Python을 찾지 못했습니다: $python. MSYS2 설치 경로 또는 -MsysRoot를 확인하세요."
    }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
        throw 'Visual Studio Installer의 vswhere.exe가 없습니다. C++ 데스크톱 개발 도구 설치를 확인하세요.'
    }
    $vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or -not $vsRoot) {
        throw 'C++ 데스크톱 개발 도구가 포함된 Visual Studio를 찾지 못했습니다.'
    }
    $vsVersion = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion
    if ($LASTEXITCODE -ne 0 -or -not $vsVersion) {
        throw '설치된 Visual Studio 버전을 조회하지 못했습니다. Visual Studio Installer 상태를 확인하세요.'
    }
    $cmakeCommand = Get-Command cmake.exe -ErrorAction SilentlyContinue
    $gitCommand = Get-Command git.exe -ErrorAction SilentlyContinue
    if (-not $cmakeCommand) { throw 'CMake를 찾지 못했습니다. 설치 후 cmake.exe 경로를 PATH에 등록하세요.' }
    if (-not $gitCommand) { throw 'Git을 찾지 못했습니다. 설치 후 git.exe 경로를 PATH에 등록하세요.' }
    $cmake = $cmakeCommand.Source
    $git = $gitCommand.Source
    $arguments = @(
        '-B',
        (Join-Path $PSScriptRoot 'third_party/mpv_build/build_windows.py'),
        '--project', $PSScriptRoot, '--msys', $MsysRoot,
        '--visual-studio', $vsRoot, '--visual-studio-version', $vsVersion,
        '--cmake', $cmake, '--git', $git
    )
    if ($CheckOnly) { $arguments += '--check-only' }
    & $python @arguments
    if ($LASTEXITCODE -ne 0) { throw "SDK 준비 실패 (종료 코드 $LASTEXITCODE). 위 실패 단계·사용 버전·로그 경로를 확인하세요." }
} catch {
    [Console]::Error.WriteLine('[FAIL] ' + $_.Exception.Message)
    exit 1
} finally {
    [Console]::OutputEncoding = $previousEncoding
    $env:PYTHONIOENCODING = $previousPythonEncoding
}
