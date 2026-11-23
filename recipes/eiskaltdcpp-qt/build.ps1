# recipes/eiskaltdcpp-qt/build.ps1 -- the EiskaltDC++ Qt 6 desktop client for Windows, with MSVC,
# against cvcpkg's shared dependencies (Qt 6 included). The core library is linked in
# statically, as the upstream Windows build does.
#
# Every input except the compiler is a cvcpkg package: the libraries from the dependency prefix,
# and cmake, ninja and gettext's msgfmt from the build or dependency prefix (never the
# machine's). The compiler is MSVC: a Visual Studio developer environment, or a Visual Studio
# with the C++ workload that vswhere can find.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$ProgressPreference = 'SilentlyContinue'

foreach ($name in 'CVC_SOURCE_DIR', 'CVC_BUILD_DIR', 'CVC_INSTALL_DIR', 'CVC_DEPS_PREFIX') {
    if (-not [Environment]::GetEnvironmentVariable($name)) { throw "$name must be set" }
}
$jobs = if ($env:CVC_JOBS) { $env:CVC_JOBS } else { [Environment]::ProcessorCount }
$buildType = if ("$env:CVC_BUILD_TYPE".ToLower() -eq 'debug') { 'Debug' } else { 'Release' }
# cvcpkg's Windows dependencies are DLLs built against the dynamic CRT (/MD), and C++ objects
# cross those DLL boundaries (Qt, protobuf), so the client uses the dynamic CRT too.
$msvcRuntime = if ($buildType -eq 'Debug') { 'MultiThreadedDebugDLL' } else { 'MultiThreadedDLL' }

function Invoke-Checked {
    param([Parameter(Mandatory)][string]$Exe, [string[]]$Arguments = @())
    Write-Host "+ $Exe $($Arguments -join ' ')"
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Exe failed with exit code $LASTEXITCODE" }
}

# CMake reads backslashes in a -D value as escapes once it lands in a list or a string.
function ConvertTo-CMakePath([string]$Path) { $Path -replace '\\', '/' }

$cvcRoots = @($env:CVC_BUILD_PREFIX, $env:CVC_DEPS_PREFIX) | Where-Object { $_ } |
    ForEach-Object { [IO.Path]::GetFullPath($_) } | Select-Object -Unique

function Find-CvcFile([string]$RelPath, [string]$Package) {
    foreach ($root in $cvcRoots) {
        $p = Join-Path $root $RelPath
        if (Test-Path -LiteralPath $p) { return $p }
    }
    throw "$RelPath (cvcpkg package '$Package') is in neither the build nor the dependency prefix: run cvcpkg install-deps eiskaltdcpp-qt --include-host-tools"
}

function Import-MsvcEnv {
    if (Get-Command cl.exe -ErrorAction SilentlyContinue) { return }
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path -LiteralPath $vswhere)) { throw 'cl.exe is not on PATH and vswhere.exe was not found' }
    $vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vsRoot) { throw 'vswhere found no Visual Studio with the C++ workload' }
    $vcvars = Join-Path $vsRoot 'VC\Auxiliary\Build\vcvars64.bat'
    $lines = & cmd.exe /d /s /c "`"$vcvars`" >nul 2>&1 && set"
    if ($LASTEXITCODE -ne 0) { throw "$vcvars failed" }
    foreach ($line in $lines) {
        $i = $line.IndexOf('=')
        if ($i -gt 0) { [Environment]::SetEnvironmentVariable($line.Substring(0, $i), $line.Substring($i + 1)) }
    }
    if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) { throw 'vcvars64.bat ran but cl.exe is still not on PATH' }
}
Import-MsvcEnv

# Git for Windows puts its coreutils link.exe on PATH; MSVC's must come first.
$clDir = Split-Path -Parent (Get-Command cl.exe -CommandType Application | Select-Object -First 1).Source
$cmake  = Find-CvcFile 'bin\cmake.exe' 'cmake'
$ninja  = Find-CvcFile 'bin\ninja.exe' 'ninja'
$msgfmt = Find-CvcFile 'bin\msgfmt.exe' 'gettext'
# protoc (protobuf) and the dependency DLLs, for the build's code generators.
$env:PATH = "$clDir;$(Join-Path $env:CVC_DEPS_PREFIX 'bin');$(Split-Path -Parent $msgfmt);$env:PATH"
& $cmake --version | Select-Object -First 1
Write-Host "cmake  $cmake`nninja  $ninja`nmsgfmt $msgfmt"

$src = $env:CVC_SOURCE_DIR
$prefixPath = ($cvcRoots | ForEach-Object { ConvertTo-CMakePath $_ }) -join ';'
# The Windows build installs EiskaltDC++.exe and its resources\ at the top of the install prefix
# (BINDIR "."), and looks for them beside the executable. Here that is bin\, beside the DLLs of
# Qt and the other dependencies.
$bin = Join-Path $env:CVC_INSTALL_DIR 'bin'
Invoke-Checked $cmake @(
    '-G', 'Ninja', '-S', $src, '-B', $env:CVC_BUILD_DIR,
    "-DCMAKE_MAKE_PROGRAM=$(ConvertTo-CMakePath $ninja)",
    "-DCMAKE_BUILD_TYPE=$buildType",
    "-DCMAKE_INSTALL_PREFIX=$(ConvertTo-CMakePath $bin)",
    "-DCMAKE_PREFIX_PATH=$prefixPath",
    "-DGETTEXT_SEARCH_PATH=$(ConvertTo-CMakePath $env:CVC_DEPS_PREFIX)",
    "-DGETTEXT_MSGFMT_EXECUTABLE=$(ConvertTo-CMakePath $msgfmt)",
    "-DCMAKE_MSVC_RUNTIME_LIBRARY=$msvcRuntime",
    '-DCMAKE_C_COMPILER=cl', '-DCMAKE_CXX_COMPILER=cl',
    # The same features as the eiskaltdcpp recipe's core.
    '-DLUA_SCRIPT=ON', '-DWITH_DHT=ON', '-DWITH_NMDCPB=ON',
    '-DUSE_MINIUPNP=ON', '-DPERL_REGEX=ON', '-DUSE_IDN2=ON', '-DLOCAL_JSONCPP=ON',
    # The Qt 6 client only; see the recipe's notes for what cvcpkg's Qt lacks.
    '-DUSE_QT6=ON', '-DUSE_QT_QML=OFF', '-DUSE_JS=OFF', '-DUSE_QT_SQLITE=ON', '-DUSE_ASPELL=OFF',
    '-DDBUS_NOTIFY=OFF', '-DUSE_GTK3=OFF',
    '-DJSONRPC_DAEMON=OFF', '-DXMLRPC_DAEMON=OFF', '-DNO_UI_DAEMON=OFF',
    '-DUSE_CLI_JSONRPC=OFF', '-DUSE_CLI_XMLRPC=OFF',
    '-DWITH_DEV_FILES=OFF', '-DEISKALTDCPP_SDK=OFF',
    '-DWITH_EMOTICONS=ON', '-DWITH_SOUNDS=ON', '-DWITH_EXAMPLES=OFF', '-DWITH_LUASCRIPTS=ON',
    '-DOPENSSL_MSVC=ON', '-DINSTALL_DEPENDENCIES=OFF', '-DINSTALL_QT_TRANSLATIONS=OFF',
    '-DUSE_CCACHE=OFF', '-DBUILD_TESTS=OFF'
)
Invoke-Checked $cmake @('--build', $env:CVC_BUILD_DIR, '-j', "$jobs")
Invoke-Checked $cmake @('--install', $env:CVC_BUILD_DIR)

$exe = Join-Path $bin 'EiskaltDC++.exe'
if (-not (Test-Path -LiteralPath $exe)) { throw 'bin\EiskaltDC++.exe was not installed' }
Get-ChildItem $bin -Filter '*.pdb' | Remove-Item -Force
Write-Host "cvcpkg: eiskaltdcpp-qt -> $env:CVC_INSTALL_DIR (bin\EiskaltDC++.exe and bin\resources\)"
