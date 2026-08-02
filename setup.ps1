<#
.SYNOPSIS
    Bootstraps the C+ compiler using an offline TinyCC archive and supports automatic git updates.

.DESCRIPTION
    Builds the C+ compiler using a local TCC archive, manages PATH,
    stores installation metadata, checks GitHub for compiler/libc+
    updates, and performs git pull and rebuilds upon user confirmation.
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

$UpdateState = Join-Path $Root ".cplus-update.json"

$CompilerManifest = Join-Path $Root "manifest.json"
$LibcpManifest = Join-Path $Root "libc+\manifest.json"

$RemoteCompilerManifest =
    "https://raw.githubusercontent.com/Sonnigg/cplus/main/manifest.json"

$RemoteLibcpManifest =
    "https://raw.githubusercontent.com/Sonnigg/cplus/main/libc%2B/manifest.json"


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

    Write-Host "[$Level] $Message" -ForegroundColor Blue
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
# Manifest Handling
# ------------------------------------------------------------

function Get-LocalVersion {

    if (
        -not (Test-Path $CompilerManifest) -or
        -not (Test-Path $LibcpManifest)
    ) {
        return $null
    }


    $compiler =
        Get-Content $CompilerManifest -Raw |
        ConvertFrom-Json


    $libcp =
        Get-Content $LibcpManifest -Raw |
        ConvertFrom-Json


    return @{
        compiler = $compiler.version
        libcp = $libcp.version
    }
}


function Get-RemoteVersion {

    param(
        [Parameter(Mandatory)]
        [string]$Url
    )


    try {

        $manifest =
            Invoke-RestMethod `
                -Uri $Url `
                -UseBasicParsing


        return $manifest.version

    }
    catch {

        return $null

    }
}


# ------------------------------------------------------------
# Update Checking & Git Pull
# ------------------------------------------------------------

function Save-UpdateState {

    param(
        [string]$Compiler,
        [string]$Libcp
    )


    @{
        compiler = $Compiler
        libcp = $Libcp
        lastCheck = (Get-Date).ToUniversalTime()
    }
    |
    ConvertTo-Json
    |
    Set-Content $UpdateState
}


function Check-ForUpdates {

    if (-not (Test-Path $UpdateState)) {
        return $false
    }


    $state =
        Get-Content $UpdateState -Raw |
        ConvertFrom-Json


    $lastCheck =
        [DateTime]$state.lastCheck


    if (
        ((Get-Date).ToUniversalTime() - $lastCheck).TotalHours -lt 24
    ) {
        return $false
    }


    Write-Log "Checking for C+ updates..."


    $latestCompiler =
        Get-RemoteVersion $RemoteCompilerManifest


    $latestLibcp =
        Get-RemoteVersion $RemoteLibcpManifest


    if (
        $null -eq $latestCompiler -or
        $null -eq $latestLibcp
    ) {
        return $false
    }


    $changed = $false


    if ($latestCompiler -ne $state.compiler) {

        Write-Host ""
        Write-Host "Compiler update available!" `
            -ForegroundColor Yellow

        Write-Host "Installed: $($state.compiler)"
        Write-Host "Latest:    $latestCompiler"

        $changed = $true
    }


    if ($latestLibcp -ne $state.libcp) {

        Write-Host ""
        Write-Host "libc+ update available!" `
            -ForegroundColor Yellow

        Write-Host "Installed: $($state.libcp)"
        Write-Host "Latest:    $latestLibcp"

        $changed = $true
    }


    $shouldRebuild = $false

    if ($changed) {

        Write-Host ""

        $answer =
            Read-Host "Update now? [Y/n]"


        if (
            $answer -eq "" -or
            $answer -match "^[Yy]"
        ) {

            Write-Log "Pulling latest updates via git..."

            $git = Get-Command git -ErrorAction SilentlyContinue
            if ($null -eq $git) {
                Write-ErrorLog "Git is not installed or not found in PATH. Cannot perform automatic update."
            } else {
                Push-Location $Root
                try {
                    & git pull
                    if ($LASTEXITCODE -ne 0) {
                        throw "git pull exited with code $LASTEXITCODE"
                    }
                    Write-Success "Repository updated successfully."
                    $shouldRebuild = $true
                }
                catch {
                    Write-ErrorLog "Git pull failed: $_"
                }
                finally {
                    Pop-Location
                }
            }
        }
    }


    Save-UpdateState `
        -Compiler $latestCompiler `
        -Libcp $latestLibcp

    return $shouldRebuild
}


# ------------------------------------------------------------
# PATH Handling
# ------------------------------------------------------------

function Add-ToUserPath {

    param(
        [Parameter(Mandatory)]
        [string]$Directory
    )


    $Directory =
        [System.IO.Path]::GetFullPath($Directory)
            .TrimEnd("\")


    $userPath =
        [Environment]::GetEnvironmentVariable(
            "Path",
            [EnvironmentVariableTarget]::User
        )


    if (-not $userPath) {
        $userPath = ""
    }


    $entries =
        @(
            $userPath -split ";" |
            Where-Object {
                $_.Trim() -ne ""
            }
        )


    if (
        $entries |
        Where-Object {
            $_.TrimEnd("\") -ieq $Directory
        }
    ) {

        Write-Log "Already in PATH: $Directory"
        return
    }


    $entries += $Directory


    [Environment]::SetEnvironmentVariable(
        "Path",
        ($entries -join ";"),
        [EnvironmentVariableTarget]::User
    )


    $env:Path =
        ($env:Path -split ";" + $Directory) -join ";"


    Write-Log "Added PATH: $Directory"
}


# ------------------------------------------------------------
# Build Logic
# ------------------------------------------------------------

function Invoke-CPlusBuild {

    if (-not (Test-Path $MainSrc)) {
        throw "Missing source: $MainSrc"
    }


    $sources = @($MainSrc)


    Get-ChildItem $SrcWildcard -File |
    ForEach-Object {
        $sources += $_.FullName
    }


    $tcc =
        Get-Command tcc -ErrorAction SilentlyContinue


    if ($null -ne $tcc) {

        $TCC = $tcc.Source
        $TCCDir = Split-Path $TCC

        Write-Success "Found TCC: $TCC"

    }

    else {

        Write-Log "Searching local TCC archive..."


        $arch =
            [System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture


        $filter =
            switch ($arch) {

                X64 { "*64*.zip" }
                X86 { "*32*.zip" }

                default {
                    throw "Unsupported architecture."
                }
            }


        $zip =
            Get-ChildItem `
                (Join-Path $Root "winzips") `
                -Filter $filter |
            Select-Object -First 1


        if ($null -eq $zip) {
            throw "No TCC archive found."
        }


        Expand-Archive `
            $zip.FullName `
            $Tools `
            -Force


        $TCCExe =
            Get-ChildItem `
                $Tools `
                -Filter tcc.exe `
                -Recurse |
            Select-Object -First 1


        if ($null -eq $TCCExe) {
            throw "Could not find extracted TCC."
        }


        $TCC = $TCCExe.FullName
        $TCCDir = $TCCExe.DirectoryName

    }



    New-Item `
        -ItemType Directory `
        -Force `
        $Bin `
    | Out-Null



    foreach ($exe in $Executables) {

        $output =
            Join-Path $Bin $exe


        Write-Log "Building $exe..."


        & $TCC `
            "-I$IncludeDir" `
            -o $output `
            @sources


        if ($LASTEXITCODE -ne 0) {
            throw "Compilation failed."
        }

    }



    $versions = Get-LocalVersion


    if ($null -ne $versions) {

        Save-UpdateState `
            $versions.compiler `
            $versions.libcp
    }



    Add-ToUserPath $Bin
    Add-ToUserPath $Tools
    Add-ToUserPath $TCCDir

}


# ------------------------------------------------------------
# Execution Flow
# ------------------------------------------------------------

try {

    Write-Log "Starting C+ bootstrap..." "BOOTSTRAP"


    $performRebuild = Check-ForUpdates


    if ($performRebuild -or -not (Test-Path (Join-Path $Bin "cplus.exe"))) {
        Invoke-CPlusBuild
    } else {
        Write-Log "No rebuild needed."
    }


    Write-Success "C+ successfully installed/updated."

}

catch {

    Write-ErrorLog $_.Exception.Message
    exit 1

}