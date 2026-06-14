param(
    [string] $Version = "latest",
    [string] $InstallDir = "",
    [string] $BinDir = "",
    [string] $Command = "uvc",
    [switch] $UseUv,
    [switch] $Both,
    [string] $PythonUvCommand = "pyuv",
    [switch] $NoPath,
    [switch] $Yes,
    [string] $SourceUrl = "",
    [string] $ChecksumUrl = "",
    [string] $ChecksumFile = "",
    [string] $FromLocal = "",
    [switch] $DryRun
)

$ErrorActionPreference = "Stop"

$Repository = "blacklight-foundation/ultraviolet"
$AssetName = "ultraviolet-windows-x86_64.zip"

function Write-InstallLog {
    param([string] $Message)
    Write-Host $Message
}

function Fail-Install {
    param([string] $Message)
    throw $Message
}

function Get-DefaultInstallDir {
    if ($env:LOCALAPPDATA) {
        return (Join-Path $env:LOCALAPPDATA "Ultraviolet\current")
    }
    return (Join-Path $HOME "AppData\Local\Ultraviolet\current")
}

function Get-DefaultBinDir {
    if ($env:LOCALAPPDATA) {
        return (Join-Path $env:LOCALAPPDATA "Ultraviolet\bin")
    }
    return (Join-Path $HOME "AppData\Local\Ultraviolet\bin")
}

function Normalize-FullPath {
    param([string] $Path)
    return [System.IO.Path]::GetFullPath($Path)
}

function Test-InsideDirectory {
    param(
        [string] $Path,
        [string] $Directory
    )
    $fullPath = Normalize-FullPath $Path
    $fullDirectory = Normalize-FullPath $Directory
    return $fullPath.StartsWith(
        $fullDirectory.TrimEnd('\') + "\",
        [System.StringComparison]::OrdinalIgnoreCase
    ) -or $fullPath.Equals($fullDirectory, [System.StringComparison]::OrdinalIgnoreCase)
}

function Get-ExistingUv {
    $commandInfo = Get-Command uv -CommandType Application -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if (-not $commandInfo) {
        return $null
    }

    $versionText = ""
    try {
        $versionText = & $commandInfo.Source --version 2>$null
    } catch {
        $versionText = ""
    }

    [pscustomobject]@{
        Path = $commandInfo.Source
        Version = [string] $versionText
    }
}

function Invoke-Download {
    param(
        [string] $Url,
        [string] $OutputPath
    )
    Invoke-WebRequest -Uri $Url -OutFile $OutputPath -UseBasicParsing
}

function Get-Sha256Hex {
    param([string] $Path)

    $fileHashCommand = Get-Command Get-FileHash -CommandType Cmdlet -ErrorAction SilentlyContinue
    if ($fileHashCommand) {
        return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
    }

    $sha256 = [System.Security.Cryptography.SHA256]::Create()
    $stream = [System.IO.File]::OpenRead($Path)
    try {
        $hash = $sha256.ComputeHash($stream)
        return [System.BitConverter]::ToString($hash).Replace("-", "").ToLowerInvariant()
    } finally {
        $stream.Dispose()
        $sha256.Dispose()
    }
}

function Verify-Checksum {
    param(
        [string] $ArchivePath,
        [string] $ChecksumPath
    )
    $text = Get-Content -LiteralPath $ChecksumPath -Raw
    if ($text -notmatch "([A-Fa-f0-9]{64})") {
        Fail-Install "checksum file did not contain a SHA-256 hash"
    }

    $expected = $Matches[1].ToLowerInvariant()
    $actual = Get-Sha256Hex $ArchivePath
    if ($actual -ne $expected) {
        Fail-Install "checksum mismatch for $ArchivePath"
    }
}

function Find-PackageRoot {
    param([string] $ExtractDir)

    $candidates = @(
        (Join-Path $ExtractDir "ultraviolet"),
        (Join-Path $ExtractDir "ultraviolet-windows-x86_64"),
        $ExtractDir
    )

    foreach ($candidate in $candidates) {
        if ((Test-Path -LiteralPath (Join-Path $candidate "uv.exe")) -or
            (Test-Path -LiteralPath (Join-Path $candidate "uvc.exe"))) {
            return $candidate
        }
    }

    $found = Get-ChildItem -LiteralPath $ExtractDir -Recurse -File `
        -Include "uv.exe", "uvc.exe" |
        Select-Object -First 1
    if (-not $found) {
        Fail-Install "could not find uv.exe or uvc.exe in package"
    }
    return $found.Directory.FullName
}

function Copy-Package {
    param(
        [string] $PackageRoot,
        [string] $StageDir
    )
    New-Item -ItemType Directory -Force -Path $StageDir | Out-Null
    Copy-Item -Path (Join-Path $PackageRoot "*") -Destination $StageDir -Recurse -Force
}

function New-CmdShim {
    param(
        [string] $Name,
        [string] $Target
    )
    if (-not (Test-Path -LiteralPath $Target)) {
        Fail-Install "cannot create $Name shim; target missing: $Target"
    }

    New-Item -ItemType Directory -Force -Path $BinDir | Out-Null
    $shimPath = Join-Path $BinDir "$Name.cmd"
    $escapedTarget = $Target.Replace('"', '""')
    $content = @(
        "@echo off",
        "setlocal",
        "`"$escapedTarget`" %*",
        "exit /b %ERRORLEVEL%"
    ) -join "`r`n"
    Set-Content -LiteralPath $shimPath -Value ($content + "`r`n") -Encoding Ascii
}

function Ensure-UserPath {
    param([string] $Directory)

    $current = [Environment]::GetEnvironmentVariable("Path", "User")
    if (-not $current) {
        $current = ""
    }

    $entries = $current -split ";" | Where-Object { $_ -ne "" }
    foreach ($entry in $entries) {
        if ($entry.Equals($Directory, [System.StringComparison]::OrdinalIgnoreCase)) {
            return
        }
    }

    $updated = if ($current.Trim()) {
        "$current;$Directory"
    } else {
        $Directory
    }

    [Environment]::SetEnvironmentVariable("Path", $updated, "User")
    if (($env:Path -split ";") -notcontains $Directory) {
        $env:Path = "$Directory;$env:Path"
    }
    Write-InstallLog "Added $Directory to the user PATH."
}

if ($env:ULTRAVIOLET_INSTALL_VERSION -and -not $PSBoundParameters.ContainsKey("Version")) {
    $Version = $env:ULTRAVIOLET_INSTALL_VERSION
}
if ($env:ULTRAVIOLET_INSTALL_DIR -and -not $PSBoundParameters.ContainsKey("InstallDir")) {
    $InstallDir = $env:ULTRAVIOLET_INSTALL_DIR
}
if ($env:ULTRAVIOLET_BIN_DIR -and -not $PSBoundParameters.ContainsKey("BinDir")) {
    $BinDir = $env:ULTRAVIOLET_BIN_DIR
}
if ($env:ULTRAVIOLET_INSTALL_COMMAND -and -not $PSBoundParameters.ContainsKey("Command")) {
    $Command = $env:ULTRAVIOLET_INSTALL_COMMAND
}
if ($env:ULTRAVIOLET_INSTALL_PYTHON_UV_COMMAND -and -not $PSBoundParameters.ContainsKey("PythonUvCommand")) {
    $PythonUvCommand = $env:ULTRAVIOLET_INSTALL_PYTHON_UV_COMMAND
}
if ($env:ULTRAVIOLET_INSTALL_SOURCE_URL -and -not $PSBoundParameters.ContainsKey("SourceUrl")) {
    $SourceUrl = $env:ULTRAVIOLET_INSTALL_SOURCE_URL
}
if ($env:ULTRAVIOLET_INSTALL_CHECKSUM_URL -and -not $PSBoundParameters.ContainsKey("ChecksumUrl")) {
    $ChecksumUrl = $env:ULTRAVIOLET_INSTALL_CHECKSUM_URL
}
if ($env:ULTRAVIOLET_INSTALL_CHECKSUM_FILE -and -not $PSBoundParameters.ContainsKey("ChecksumFile")) {
    $ChecksumFile = $env:ULTRAVIOLET_INSTALL_CHECKSUM_FILE
}
if ($env:ULTRAVIOLET_INSTALL_NO_PATH -and -not $PSBoundParameters.ContainsKey("NoPath")) {
    $NoPath = $true
}
if ($env:ULTRAVIOLET_INSTALL_YES -and -not $PSBoundParameters.ContainsKey("Yes")) {
    $Yes = $true
}

if (-not $InstallDir) {
    $InstallDir = Get-DefaultInstallDir
}
if (-not $BinDir) {
    $BinDir = Get-DefaultBinDir
}

if ($UseUv) {
    $Command = "uv"
}
if ($Both) {
    $Command = "both"
}

$Command = $Command.ToLowerInvariant()
if (@("uvc", "uv", "both") -notcontains $Command) {
    Fail-Install "-Command must be one of: uvc, uv, both"
}
if (-not $PythonUvCommand -or
    $PythonUvCommand.Contains("/") -or
    $PythonUvCommand.Contains("\") -or
    $PythonUvCommand -notmatch "^[A-Za-z0-9._-]+$") {
    Fail-Install "-PythonUvCommand must be a command name containing only letters, digits, '.', '_', and '-'"
}
if (@("uv", "uvc") -contains $PythonUvCommand.ToLowerInvariant()) {
    Fail-Install "-PythonUvCommand cannot be uv or uvc"
}

if (-not $SourceUrl -and -not $FromLocal) {
    if ($Version -eq "latest") {
        $SourceUrl = "https://github.com/$Repository/releases/latest/download/$AssetName"
    } else {
        $SourceUrl = "https://github.com/$Repository/releases/download/$Version/$AssetName"
    }
}

if (-not $ChecksumUrl -and -not $FromLocal) {
    $ChecksumUrl = "$SourceUrl.sha256"
}

if ($ChecksumFile -and -not $FromLocal) {
    Fail-Install "-ChecksumFile requires -FromLocal"
}

$existingUv = Get-ExistingUv
$pythonUvDetected = $false
if ($existingUv -and -not (Test-InsideDirectory $existingUv.Path $BinDir)) {
    if ($existingUv.Version -match "^uv\s+[0-9]") {
        $pythonUvDetected = $true
    }
}

function Test-CommandInstallsUv {
    param([string] $CommandMode)
    return $CommandMode -eq "uv" -or $CommandMode -eq "both"
}

function Write-PythonUvConflictWarning {
    Write-Host ""
    Write-Host "Warning: installing Ultraviolet as uv can shadow Python uv."
    Write-Host "Detected Python uv: $($existingUv.Path)"
    Write-Host "Version: $($existingUv.Version)"
    Write-Host "Python uv will be available as: $PythonUvCommand"
}

if ($pythonUvDetected -and (Test-CommandInstallsUv $Command)) {
    Write-PythonUvConflictWarning
    if (-not $Yes -and -not $DryRun) {
        if ([Console]::IsInputRedirected) {
            Fail-Install "Python uv conflict detected; rerun with -Yes to confirm, use the default uvc command, or choose a different -PythonUvCommand"
        }
        $answer = Read-Host "Continue? [y/N]"
        if (@("y", "yes") -notcontains $answer.ToLowerInvariant()) {
            Fail-Install "install cancelled"
        }
    }
}

Write-InstallLog "Ultraviolet installer"
Write-InstallLog "Install directory: $InstallDir"
Write-InstallLog "Shim directory: $BinDir"
Write-InstallLog "Command mode: $Command"
Write-InstallLog ("PATH update: " + ($(if ($NoPath) { "disabled" } else { "enabled" })))

if ($DryRun) {
    if ($FromLocal) {
        Write-InstallLog "Would install from local package: $FromLocal"
    }
    if ($SourceUrl) {
        Write-InstallLog "Would download: $SourceUrl"
    }
    if ($ChecksumUrl) {
        Write-InstallLog "Would verify checksum: $ChecksumUrl"
    }
    if ($ChecksumFile) {
        Write-InstallLog "Would verify local checksum: $ChecksumFile"
    }
    if ($pythonUvDetected) {
        Write-InstallLog "Detected Python uv: $($existingUv.Path)"
        if (Test-CommandInstallsUv $Command) {
            Write-InstallLog "Would preserve Python uv as: $PythonUvCommand"
        }
    }
    Write-InstallLog "Dry run complete."
    return
}

$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("ultraviolet-install-" + [guid]::NewGuid())
$extractDir = Join-Path $tempRoot "extract"
New-Item -ItemType Directory -Force -Path $extractDir | Out-Null

try {
    if ($FromLocal) {
        if (Test-Path -LiteralPath $FromLocal -PathType Container) {
            if ($ChecksumFile) {
                Fail-Install "-ChecksumFile cannot be used with a local package directory"
            }
            $packageRoot = $FromLocal
        } else {
            if ($ChecksumFile) {
                if (-not (Test-Path -LiteralPath $ChecksumFile -PathType Leaf)) {
                    Fail-Install "checksum file does not exist: $ChecksumFile"
                }
                Verify-Checksum $FromLocal $ChecksumFile
            }
            Expand-Archive -LiteralPath $FromLocal -DestinationPath $extractDir -Force
            $packageRoot = Find-PackageRoot $extractDir
        }
    } else {
        $archive = Join-Path $tempRoot $AssetName
        $checksumFile = Join-Path $tempRoot "$AssetName.sha256"
        Write-InstallLog "Downloading $SourceUrl"
        Invoke-Download $SourceUrl $archive
        Write-InstallLog "Downloading $ChecksumUrl"
        Invoke-Download $ChecksumUrl $checksumFile
        Verify-Checksum $archive $checksumFile
        Expand-Archive -LiteralPath $archive -DestinationPath $extractDir -Force
        $packageRoot = Find-PackageRoot $extractDir
    }

    $stageDir = "$InstallDir.stage"
    $backupDir = "$InstallDir.previous"
    Remove-Item -LiteralPath $stageDir -Recurse -Force -ErrorAction SilentlyContinue
    Copy-Package $packageRoot $stageDir

    Remove-Item -LiteralPath $backupDir -Recurse -Force -ErrorAction SilentlyContinue
    if (Test-Path -LiteralPath $InstallDir) {
        Move-Item -LiteralPath $InstallDir -Destination $backupDir
    }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $InstallDir) | Out-Null
    Move-Item -LiteralPath $stageDir -Destination $InstallDir

    $compilerUv = Join-Path $InstallDir "uv.exe"
    $compilerUvc = Join-Path $InstallDir "uvc.exe"
    if (-not (Test-Path -LiteralPath $compilerUvc)) {
        $compilerUvc = $compilerUv
    }
    if (-not (Test-Path -LiteralPath $compilerUv)) {
        $compilerUv = $compilerUvc
    }

    switch ($Command) {
        "uvc" {
            New-CmdShim "uvc" $compilerUvc
        }
        "uv" {
            if ($pythonUvDetected) {
                New-CmdShim $PythonUvCommand $existingUv.Path
            }
            New-CmdShim "uv" $compilerUv
        }
        "both" {
            if ($pythonUvDetected) {
                New-CmdShim $PythonUvCommand $existingUv.Path
            }
            New-CmdShim "uv" $compilerUv
            New-CmdShim "uvc" $compilerUvc
        }
    }

    if (-not $NoPath) {
        Ensure-UserPath $BinDir
    } else {
        Write-InstallLog "PATH not modified. Add this directory manually when needed:"
        Write-InstallLog "  $BinDir"
    }

    Write-InstallLog "Installed Ultraviolet."
    Write-InstallLog "Open a new shell, then run:"
    if ($Command -eq "uvc") {
        Write-InstallLog "  uvc --help"
    } elseif ($Command -eq "uv") {
        Write-InstallLog "  uv --help"
    } else {
        Write-InstallLog "  uv --help"
        Write-InstallLog "  uvc --help"
    }
    if ($pythonUvDetected -and ($Command -eq "uv" -or $Command -eq "both")) {
        Write-InstallLog "Python uv is available as:"
        Write-InstallLog "  $PythonUvCommand --version"
    }
} finally {
    Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
}
