[CmdletBinding()]
param(
    # Defaults to the pinned submodule; integrate.cmake refuses any other Latite commit.
    [string]$LatiteSource,
    [string]$BuildDirectory,
    [string]$MingwBin
)
$ErrorActionPreference = 'Stop'
$repository = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
if (-not $LatiteSource) { $LatiteSource = Join-Path $repository 'bedrock/third_party/Latite' }
if (-not (Test-Path -LiteralPath (Join-Path $LatiteSource 'CMakeLists.txt'))) {
    throw 'Latite source is missing. Run: git submodule update --init bedrock/third_party/Latite'
}
$source = (Resolve-Path -LiteralPath $LatiteSource).Path
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $repository 'bedrock/windows/out/latite' }
$savedPath = $env:PATH
try {
    if (-not $MingwBin -and -not (Get-Command ld.exe -ErrorAction SilentlyContinue)) {
        $MingwBin = @('C:\ProgramData\mingw64\mingw64\bin', 'C:\msys64\mingw64\bin', 'C:\msys64\ucrt64\bin') |
            Where-Object { Test-Path -LiteralPath (Join-Path $_ 'ld.exe') } | Select-Object -First 1
    }
    if ($MingwBin) { $env:PATH = (Resolve-Path -LiteralPath $MingwBin).Path + ';' + $env:PATH }
    # Latite embeds its assets with `ld -r -b binary`.
    if (-not (Get-Command ld.exe -ErrorAction SilentlyContinue)) { throw 'MinGW ld.exe is required. Supply -MingwBin.' }
    $integration = Join-Path $repository 'bedrock/windows/integrations/latite/integrate.cmake'
    & cmake -S $source -B $BuildDirectory -G 'Visual Studio 17 2022' -A x64 "-DCMAKE_PROJECT_Latite_INCLUDE=$integration"
    if ($LASTEXITCODE -ne 0) { throw 'Native CMake configuration failed.' }
    & cmake --build $BuildDirectory --config Release --target Latite --parallel 4
    if ($LASTEXITCODE -ne 0) { throw 'Native build failed. Do not use a stale DLL.' }
    Write-Output (Join-Path $BuildDirectory 'Release/Latite.dll')
} finally { $env:PATH = $savedPath }
