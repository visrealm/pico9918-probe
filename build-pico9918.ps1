param(
    [string]$Sdk = "$env:USERPROFILE/.pico-sdk/sdk/2.3.0",
    [string]$Toolchain = "$env:USERPROFILE/.pico-sdk/toolchain/13_3_Rel1",
    [string]$BuildDir = "$PSScriptRoot/../../build-debugprobe-pico9918"
)
$ErrorActionPreference = 'Stop'
$ninja = "$env:USERPROFILE/.pico-sdk/ninja/v1.12.1/ninja.exe"
$pioasm = "$env:USERPROFILE/.pico-sdk/tools/2.3.0/pioasm"
# SDK host tools can also be built from source with the installed MinGW compiler.
if (Test-Path 'C:/msys64/mingw64/bin/g++.exe') { $env:PATH = 'C:/msys64/mingw64/bin;' + $env:PATH }
$configure = @('-S', $PSScriptRoot, '-B', $BuildDir, '-G', 'Ninja',
    '-DPICO9918_HOST_BUS=ON', "-DPICO_SDK_PATH=$Sdk", "-DPICO_TOOLCHAIN_PATH=$Toolchain",
    '-DPICOTOOL_FORCE_FETCH_FROM_GIT=ON')
if (Test-Path $ninja) { $configure += "-DCMAKE_MAKE_PROGRAM=$ninja" }
if (Test-Path "$pioasm/pioasmConfig.cmake") { $configure += "-Dpioasm_DIR=$pioasm" }
& cmake @configure
if ($LASTEXITCODE) { throw 'CMake configure failed' }
& cmake --build $BuildDir -j 8
if ($LASTEXITCODE) { throw 'Firmware build failed' }
Write-Output "$BuildDir/debugprobe_pico9918.uf2"
