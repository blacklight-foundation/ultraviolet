$ErrorActionPreference = "Stop"

$ref = if ($env:ULTRAVIOLET_INSTALL_REF) {
    $env:ULTRAVIOLET_INSTALL_REF
} else {
    "main"
}

$url = "https://raw.githubusercontent.com/blacklight-foundation/ultraviolet/$ref/Tools/InstallUltraviolet.ps1"
$script = Invoke-RestMethod -Uri $url
& ([scriptblock]::Create($script)) @args
exit $LASTEXITCODE
