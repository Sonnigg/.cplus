$ErrorActionPreference = "Stop"

# ------------------------------------------------------------
# C+ compiler bootstrap
# ------------------------------------------------------------

$Root  = Split-Path -Parent $MyInvocation.MyCommand.Path
$Src   = Join-Path $Root "source/src/cplus.c"
$Bin   = Join-Path $Root "bin"
$Tools = Join-Path $Root "tools"

Write-Host "== C+ compiler setup =="

# ------------------------------------------------------------
# Validate source
# ------------------------------------------------------------

if (-not (Test-Path -LiteralPath $Src -PathType Leaf)) {
    Write-Error "Source file not found: $Src"
    exit 1
}

# ------------------------------------------------------------
# Find TCC
# ------------------------------------------------------------

$tcc = Get-Command tcc -ErrorAction SilentlyContinue

if ($tcc) {
    $TCC = $tcc.Source
    $TCCDir = Split-Path -Parent $TCC

    Write-Host "Found TCC: $TCC"
}
else {
    Write-Host "TCC not found. Downloading it..."

    New-Item -ItemType Directory -Force -Path $Tools | Out-Null

    $arch = [System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture

    switch ($arch) {
        "X64" {
            $archive = "tcc-0.9.27-win64-bin.zip"
        }

        "X86" {
            $archive = "tcc-0.9.27-win32-bin.zip"
        }

        default {
            Write-Error "Unsupported Windows architecture: $arch"
            exit 1
        }
    }

    $url = "https://download-mirror.savannah.gnu.org/releases/tinycc/$archive"
    $zip = Join-Path $Tools $archive
    $TCCDir = Join-Path $Tools "tcc"

    Write-Host "Downloading:"
    Write-Host "  $url"

    Invoke-WebRequest `
        -Uri $url `
        -OutFile $zip `
        -UseBasicParsing

    if (Test-Path $TCCDir) {
        Remove-Item -Recurse -Force $TCCDir
    }

    Expand-Archive `
        -LiteralPath $zip `
        -DestinationPath $Tools `
        -Force

    $TCC = Join-Path $TCCDir "tcc.exe"

    if (-not (Test-Path -LiteralPath $TCC -PathType Leaf)) {
        Write-Error "TCC was downloaded, but tcc.exe could not be found."
        exit 1
    }

    Write-Host "TCC installed locally at: $TCC"
}

# ------------------------------------------------------------
# Create output directory
# ------------------------------------------------------------

New-Item -ItemType Directory -Force -Path $Bin | Out-Null
New-Item -ItemType Directory -Force -Path $Tools | Out-Null

# ------------------------------------------------------------
# Compile C+
# ------------------------------------------------------------

foreach ($name in @("cplus.exe", "c+.exe", "cc+.exe")) {
    $output = Join-Path $Bin $name

    Write-Host ""
    Write-Host "Building $name..."

    & $TCC -o $output $Src

    if ($LASTEXITCODE -ne 0) {
        Write-Error "TCC failed while building $name."
        exit $LASTEXITCODE
    }

    Write-Host "  -> $output"
}

# ------------------------------------------------------------
# Safely add directories to User PATH
# ------------------------------------------------------------

$BinFull   = [System.IO.Path]::GetFullPath($Bin)
$ToolsFull = [System.IO.Path]::GetFullPath($Tools)
$TCCFull   = [System.IO.Path]::GetFullPath($TCCDir)

$userPath = [Environment]::GetEnvironmentVariable(
    "Path",
    [EnvironmentVariableTarget]::User
)

if (-not $userPath) {
    $userPath = ""
}

$pathEntries = @(
    $userPath -split ";" |
    ForEach-Object { $_.Trim() } |
    Where-Object { $_ -ne "" }
)

function Add-UserPathEntry {
    param(
        [string]$Entry
    )

    $normalizedEntry = $Entry.TrimEnd("\", "/")

    $exists = $pathEntries | Where-Object {
        $_.TrimEnd("\", "/") -ieq $normalizedEntry
    }

    if (-not $exists) {
        $script:pathEntries += $Entry
        Write-Host "Adding to User PATH:"
        Write-Host "  $Entry"
    }
    else {
        Write-Host "Already in User PATH:"
        Write-Host "  $Entry"
    }
}

# Public C+ executables
Add-UserPathEntry $BinFull

# C+ tools directory
Add-UserPathEntry $ToolsFull

# Actual TCC executable directory
Add-UserPathEntry $TCCFull

# Save the modified User PATH.
$newUserPath = $pathEntries -join ";"

[Environment]::SetEnvironmentVariable(
    "Path",
    $newUserPath,
    [EnvironmentVariableTarget]::User
)

# ------------------------------------------------------------
# Update PATH for this PowerShell process immediately
# ------------------------------------------------------------

$env:Path = ($pathEntries -join ";")

Write-Host ""
Write-Host "C+ compiler successfully built."
Write-Host ""
Write-Host "Available commands:"
Write-Host "  cplus"
Write-Host "  c+"
Write-Host "  cc+"
Write-Host "  tcc"
Write-Host ""
Write-Host "Added directories:"
Write-Host "  $BinFull"
Write-Host "  $ToolsFull"
Write-Host "  $TCCFull"
Write-Host ""
Write-Host "New terminals will automatically inherit the updated PATH."