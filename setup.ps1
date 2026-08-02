<#
.SYNOPSIS
    Bootstraps the C+ compiler using an offline, local TinyCC (TCC) archive.

.DESCRIPTION
    Validates source files, resolves a local TCC compiler archive from the
    'winzips' directory based on architecture, compiles the C+ binaries,
    and safely updates the user's PATH environment variable.
#>

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

# ------------------------------------------------------------
# Configuration
# ------------------------------------------------------------

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path

$MainSrc = Join-Path $Root "bootstrap\main-cplus.c"
$SrcWildcard = Join-Path $Root "bootstrap\src\*.c"
$IncludeDir = Join-Path $Root "bootstrap\include"
$Bin = Join-Path $Root "bin"
$Tools = Join-Path $Root "tools"

$Executables = @(
    "cplus.exe",
    "c+.exe",
    "cc+.exe"
)

# ------------------------------------------------------------
# Logging
# ------------------------------------------------------------

function Write-Log {
    param(
        [string]$Message,
        [string]$Level = "INFO"
    )

    Write-Host "[$Level] $Message" -ForeGroundColor Blue
}

function Write-Success {
    param(
        [string]$Message
    )

    Write-Host "[SUCCESS] $Message" -ForegroundColor Green
}

function Write-ErrorLog {
    param(
        [string]$Message
    )

    Write-Host "[ERROR] $Message" -ForegroundColor Red
}

# ------------------------------------------------------------
# PATH Handling
# ------------------------------------------------------------

function Add-ToUserPath {
    param(
        [Parameter(Mandatory)]
        [string]$Directory
    )

    $Directory = [System.IO.Path]::GetFullPath($Directory).TrimEnd('\')

    $userPath = [Environment]::GetEnvironmentVariable(
        "Path",
        [EnvironmentVariableTarget]::User
    )

    if (-not $userPath) {
        $userPath = ""
    }

    $entries = @(
        $userPath -split ";" |
        ForEach-Object { $_.Trim() } |
        Where-Object { $_ -ne "" }
    )

    $exists = $entries | Where-Object {
        $_.TrimEnd('\') -ieq $Directory
    }

    if ($exists) {
        Write-Log "Already in User PATH: $Directory"
        return
    }

    $entries += $Directory

    $newPath = $entries -join ";"

    [Environment]::SetEnvironmentVariable(
        "Path",
        $newPath,
        [EnvironmentVariableTarget]::User
    )

    # Update current PowerShell session
    $currentEntries = @(
        $env:Path -split ";" |
        Where-Object { $_.Trim() -ne "" }
    )

    $currentEntries += $Directory

    $env:Path = $currentEntries -join ";"

    Write-Log "Added to User PATH: $Directory"
}

# ------------------------------------------------------------
# Main
# ------------------------------------------------------------

try {

    Write-Log "Starting C+ compiler setup..." "BOOTSTRAP"

    # --------------------------------------------------------
    # Validate source
    # --------------------------------------------------------

    if (-not (Test-Path -LiteralPath $MainSrc -PathType Leaf)) {
        throw "Main source file not found: $MainSrc"
    }

    $sourceFiles = @($MainSrc)
    $srcFilesList = Get-ChildItem -Path $SrcWildcard -File -ErrorAction SilentlyContinue
    foreach ($file in $srcFilesList) {
        $sourceFiles += $file.FullName
    }

    # --------------------------------------------------------
    # Find TCC
    # --------------------------------------------------------

    $tccCommand = Get-Command tcc -ErrorAction SilentlyContinue

    if ($null -ne $tccCommand) {

        $TCC = $tccCommand.Source
        $TCCDir = Split-Path -Parent $TCC

        Write-Success "Found TCC: $TCC"

    } else {

        Write-Log "TCC not found. Searching local archive..."

        $arch = [System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture

        switch ($arch) {

            "X64" {
                $zipFilter = "*64*.zip"
            }

            "X86" {
                $zipFilter = "*32*.zip"
            }

            default {
                throw "Unsupported architecture: $arch"
            }
        }

        $WinZipDir = Join-Path $Root "winzips"

        if (-not (Test-Path $WinZipDir)) {
            throw "Missing winzips directory: $WinZipDir"
        }

        $archives = @(
            Get-ChildItem `
                -Path $WinZipDir `
                -Filter $zipFilter `
                -File `
                -ErrorAction SilentlyContinue
        )

        if ($archives.Count -eq 0) {
            throw "No TCC archive found matching: $zipFilter"
        }

        $archive = $archives[0].FullName

        New-Item `
            -ItemType Directory `
            -Force `
            -Path $Tools |
            Out-Null

        if (Test-Path "$Tools\tcc") {
            Remove-Item `
                -Recurse `
                -Force `
                "$Tools\tcc"
        }

        Write-Log "Extracting: $archive"

        Expand-Archive `
            -LiteralPath $archive `
            -DestinationPath $Tools `
            -Force

        $tccExe = Get-ChildItem `
            -Path $Tools `
            -Filter "tcc.exe" `
            -Recurse |
            Select-Object -First 1

        if ($null -eq $tccExe) {
            throw "Could not locate tcc.exe after extraction."
        }

        $TCC = $tccExe.FullName
        $TCCDir = $tccExe.DirectoryName

        Write-Success "Installed local TCC: $TCC"
    }

    # --------------------------------------------------------
    # Prepare directories
    # --------------------------------------------------------

    New-Item -ItemType Directory -Force -Path $Bin | Out-Null
    New-Item -ItemType Directory -Force -Path $Tools | Out-Null

    # --------------------------------------------------------
    # Build compiler
    # --------------------------------------------------------

    foreach ($name in $Executables) {

        $output = Join-Path $Bin $name

        Write-Log "Building $name..."

        & $TCC "-I$IncludeDir" -o $output @sourceFiles

        if ($LASTEXITCODE -ne 0) {
            throw "TCC failed while building $name."
        }

        Write-Success "Built: $output"
    }

    # --------------------------------------------------------
    # PATH
    # --------------------------------------------------------

    Add-ToUserPath $Bin
    Add-ToUserPath $Tools
    Add-ToUserPath $TCCDir

    Write-Host ""

    Write-Success "C+ compiler successfully built."

    Write-Host ""
    Write-Host "Available commands:"
    Write-Host "  cplus"
    Write-Host "  c+"
    Write-Host "  cc+"
    Write-Host "  tcc"

    Write-Host ""
    Write-Host "New terminals will inherit the updated PATH."

}
catch {

    Write-ErrorLog $_.Exception.Message
    exit 1

}