param(
    [switch]$FrameworkDependent
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$out = Join-Path $root "publish\win-x64"
$project = Join-Path $root "src\BmsHost.Win\BmsHost.Win.csproj"

if (Test-Path $out) { Remove-Item -Recurse -Force $out }
$selfContained = if ($FrameworkDependent) { "false" } else { "true" }

dotnet publish $project `
    -c Release `
    -r win-x64 `
    --self-contained $selfContained `
    -p:PublishSingleFile=true `
    -p:IncludeNativeLibrariesForSelfExtract=true `
    -o $out

Write-Host "Published: $out"
