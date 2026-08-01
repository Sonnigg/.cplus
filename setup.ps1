<#
.SYNOPSIS
    Bootstraps the C+ compiler using TinyCC (TCC).
.DESCRIPTION
    Validates source files, resolves or downloads a compatible TCC compiler 
    (including ARM architectures), compiles the C+ binaries, and safely 
    updates the user's PATH environment variable.
#>

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

# --- Configuration ---
$Root  = Split-Path -Parent $MyInvocation.MyCommand.Path
$Src   = Join-Path $Root "source\src\cplus.c"
$Bin   = Join-Path $Root "bin"
$Tools = Join-Path $Root "tools"
$Executables = @("cplus.exe", "c+.exe", "cc+.exe")

# --- Helper Functions ---
function Write-Log { param([string]$Message, [string]$Level="INFO") Write-Host "[$Level] $Message" }
function Write-Success { param([string]$Message) Write-Host "[SUCCESS] $Message" -ForegroundColor Green }
function Write-ErrorLog { param([string]$Message) Write-Host "[ERROR] $Message" -ForegroundColor Red }

function Get-TccDownloadUrl {
    $arch = [System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture
    switch ($arch) {
        "X64"   { return "https://download-mirror.savannah.gnu.org/releases/tinycc/tcc-0.9.27-win64-bin.zip" }
        "X86"   { return "https://download-mirror.savannah.gnu.org/releases/tinycc/tcc-0.9.27-win32-bin.zip" }
        default { throw "Unsupported Windows architecture: $arch" }
    }
}

function Add-ToUserPath {
    param([string]$NewDir)
    
    $NewDir = [System.IO.Path]::GetFullPath($NewDir).TrimEnd('\')
    $userPath = [Environment]::GetEnvironmentVariable("Path", "User")
    if (-not $userPath) { $userPath = "" }
    
    $pathEntries = @($userPath -split ";" | Where-Object { $_.Trim() -ne "" })
    
    $exists = $pathEntries | Where-Object { $_.TrimEnd('\') -ieq $NewDir }
    
    if (-not $exists) {
        $pathEntries += $NewDir
        $newUserPath = $pathEntries -join ";"
        [Environment]::SetEnvironmentVariable("Path", $newUserPath, "User")
        $env:Path = ($env:Path -split ";" | Where-Object { $_.Trim() -ne "" }) + $NewDir -join ";"
        Write-Log "Added to User PATH: $NewDir"
    } else {
        Write-Log "Already in User PATH: $NewDir"
    }
}

# --- Main Execution ---
try {
    Write-Log "Starting C+ compiler setup..." "BOOTSTRAP"

    # 1. Validate Source
    if (-not (Test-Path -LiteralPath $Src -PathType Leaf)) {
        throw "Source file not found at: $Src"
    }

    # 2. Resolve TCC
    $tccCmd = Get-Command tcc -ErrorAction SilentlyContinue
    if ($tccCmd) {
        $TCC = $tccCmd.Source
        $TCCDir = Split-Path -Parent $TCC
        Write-Success "Found existing TCC installation: $TCC"
    } else {
        Write-Log "TCC not found locally. Preparing download..."
        
        $url = Get-TccDownloadUrl
        $archiveName = Split-Path $url -Leaf
        $zipPath = Join-Path $Tools $archiveName
        $TCCDir = Join-Path $Tools "tcc"
        $TCC = Join-Path $TCCDir "tcc.exe"

        New-Item -ItemType Directory -Force -Path $Tools | Out-Null
        
        Write-Log "Downloading TCC from $url"
        Invoke-WebRequest -Uri $url -OutFile $zipPath -UseBasicParsing
        
        if (Test-Path $TCCDir) { Remove-Item -Recurse -Force $TCCDir }
        
        Write-Log "Extracting archive..."
        Expand-Archive -LiteralPath $zipPath -DestinationPath $Tools -Force
        
        if (-not (Test-Path -LiteralPath $TCC -PathType Leaf)) {
            throw "Extraction completed, but tcc.exe is missing."
        }
        Write-Success "TCC installed to: $TCC"
    }

    # 3. Create Directories
    New-Item -ItemType Directory -Force -Path $Bin | Out-Null
    New-Item -ItemType Directory -Force -Path $Tools | Out-Null

    # 4. Compile C+
    Write-Log "Compiling binaries..."
    foreach ($name in $Executables) {
        $outputPath = Join-Path $Bin $name
        $process = Start-Process -FilePath $TCC -ArgumentList "-o `"$outputPath`" `"$Src`"" -Wait -NoNewWindow -PassThru
        
        if ($process.ExitCode -ne 0) {
            throw "TCC compilation failed for $name with exit code $($process.ExitCode)"
        }
        Write-Success "Built $name -> $outputPath"
    }

    # 5. Update PATH
    Write-Log "Updating environment variables..."
    Add-ToUserPath $Bin
    Add-ToUserPath $Tools
    Add-ToUserPath $TCCDir

    Write-Success "C+ compiler successfully built and configured."
    Write-Log "Commands available: cplus, c+, cc+, tcc"
    Write-Log "New terminals will automatically inherit the updated PATH."

} catch {
    Write-ErrorLog $_.Exception.Message
    exit 1
}