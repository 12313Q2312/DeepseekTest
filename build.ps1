<#
.SYNOPSIS
    DeepseekTest 一键编译 & 打包脚本
.DESCRIPTION
    自动检测编译器 → cmake 配置 → 编译 → 收集 DLL → 输出到 dist/
    支持 MSVC、MinGW、Clang
.PARAMETER Config
    构建配置: Release (默认) / Debug
.PARAMETER StaticLibCurl
    尝试静态链接 libcurl，生成无 DLL 依赖的单文件
.PARAMETER Clean
    清理 build 和 dist 目录后重新构建
.EXAMPLE
    .\build.ps1                    # 默认 Release
    .\build.ps1 -Config Debug      # Debug 模式
    .\build.ps1 -Clean             # 清理重建
    .\build.ps1 -StaticLibCurl     # 尝试静态链接
#>

param(
    [ValidateSet("Release", "Debug")]
    [string]$Config = "Release",

    [switch]$StaticLibCurl,

    [switch]$Clean
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDir  = Join-Path $ScriptDir "build"
$DistDir   = Join-Path $ScriptDir "dist"

# ---- 颜色输出 ----
function Write-Step ($msg)   { Write-Host "`n>>> $msg" -ForegroundColor Cyan }
function Write-OK ($msg)     { Write-Host "  [OK] $msg" -ForegroundColor Green }
function Write-Warn ($msg)   { Write-Host "  [!]  $msg" -ForegroundColor Yellow }
function Write-Err ($msg)    { Write-Host "  [X]  $msg" -ForegroundColor Red }

# ---- 1. 清理 ----
if ($Clean) {
    Write-Step "清理旧构建..."
    if (Test-Path $BuildDir) { Remove-Item -Recurse -Force $BuildDir }
    if (Test-Path $DistDir)  { Remove-Item -Recurse -Force $DistDir }
}

# ---- 2. 检测编译器 ----
Write-Step "检测可用编译器..."

# 查找 cmake
$cmake = Get-Command cmake -ErrorAction SilentlyContinue
if (-not $cmake) {
    Write-Err "未找到 cmake，请安装: https://cmake.org/download/"
    exit 1
}
Write-OK "cmake: $($cmake.Source)"

# 选择生成器
$generator = $null
if (Get-Command cl -ErrorAction SilentlyContinue) {
    $generator = "Visual Studio 17 2022"
    Write-OK "编译器: MSVC (Visual Studio 2022)"
}
elseif (Get-Command g++ -ErrorAction SilentlyContinue) {
    $generator = "MinGW Makefiles"
    Write-OK "编译器: MinGW GCC $(g++ -dumpversion)"
}
elseif (Get-Command clang++ -ErrorAction SilentlyContinue) {
    $generator = "MinGW Makefiles"
    Write-OK "编译器: Clang"
}
else {
    Write-Err "未找到 C++ 编译器 (MSVC / GCC / Clang)"
    Write-Err "安装选项:"
    Write-Err "  - Visual Studio 2022 Community (免费)"
    Write-Err "  - MSYS2: pacman -S mingw-w64-x86_64-gcc"
    exit 1
}

# ---- 3. 检查 libcurl ----
Write-Step "检查 libcurl..."
$curlPkgConfig = $false
if (Get-Command pkg-config -ErrorAction SilentlyContinue) {
    $curlVer = pkg-config --modversion libcurl 2>$null
    if ($curlVer) {
        Write-OK "libcurl (pkg-config): $curlVer"
        $curlPkgConfig = $true
    }
}
if (-not $curlPkgConfig) {
    Write-Warn "未通过 pkg-config 检测到 libcurl"
    Write-Warn "cmake 将自动搜索常见路径 (MSYS2/vcpkg/系统目录)"
}

# ---- 4. CMake 配置 ----
Write-Step "CMake 配置 ($Config)..."

$cmakeArgs = @(
    "-S", $ScriptDir,
    "-B", $BuildDir,
    "-G", $generator,
    "-DCMAKE_BUILD_TYPE=$Config"
)

if ($StaticLibCurl) {
    Write-Warn "尝试静态链接 libcurl (如失败请安装 libcurl 静态库)"
    $cmakeArgs += "-DDEEPSEEK_STATIC_LIBCURL=ON"
}

& $cmake.Source @cmakeArgs
if ($LASTEXITCODE -ne 0) {
    Write-Err "CMake 配置失败"
    exit 1
}
Write-OK "CMake 配置完成"

# ---- 5. 编译 ----
Write-Step "编译中..."
& $cmake.Source --build $BuildDir --config $Config
if ($LASTEXITCODE -ne 0) {
    Write-Err "编译失败"
    exit 1
}
Write-OK "编译完成"

# ---- 6. 安装到 dist/ ----
Write-Step "打包到 dist/ ..."
& $cmake.Source --install $BuildDir --prefix $DistDir --config $Config
if ($LASTEXITCODE -ne 0) {
    Write-Err "安装失败"
    exit 1
}

# ---- 7. 输出结果 ----
Write-Step "构建完成!"
$exe = Get-ChildItem -Path $DistDir -Recurse -Filter "DeepseekTest.exe" | Select-Object -First 1
if ($exe) {
    $sizeKB = [math]::Round($exe.Length / 1KB, 1)
    Write-Host ""
    Write-Host "  可执行文件: $($exe.FullName)" -ForegroundColor White
    Write-Host "  文件大小:   ${sizeKB} KB" -ForegroundColor White

    $dllCount = (Get-ChildItem -Path $DistDir -Recurse -Filter "*.dll").Count
    Write-Host "  附带 DLL:   ${dllCount} 个" -ForegroundColor White

    Write-Host ""
    Write-Host "  运行方式:" -ForegroundColor Yellow
    Write-Host "    $($exe.FullName)" -ForegroundColor White
    Write-Host ""
    Write-Host "  分发给他人: 将整个 dist/ 文件夹打包成 zip 即可" -ForegroundColor Green
}
else {
    Write-Warn "未找到 DeepseekTest.exe，请检查编译输出"
}

Write-Host ""
