$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Push-Location $root
try {
    dotnet restore .\BmsHost.sln
    dotnet build .\BmsHost.sln -c Release --no-restore
} finally {
    Pop-Location
}
