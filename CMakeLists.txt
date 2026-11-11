# ============================================================================
# DeepseekTest — CMake 构建配置
# 跨平台: Windows (MSVC/MinGW)、Linux (GCC/Clang)、macOS
# ============================================================================
cmake_minimum_required(VERSION 3.16)

project(DeepseekTest
    VERSION 1.0.0
    DESCRIPTION "控制台版 DeepSeek AI 对话客户端"
    LANGUAGES CXX
)

# ---- C++ 标准 ----
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- 选项: 静态链接 libcurl (默认 OFF，因为需要静态 OpenSSL 等依赖) ----
option(DEEPSEEK_STATIC_LIBCURL "静态链接 libcurl，生成无 DLL 依赖的单文件" OFF)

# ---- 查找 libcurl ----
# 优先使用系统包管理器安装的，其次搜索常见路径
if(DEEPSEEK_STATIC_LIBCURL)
    set(CURL_USE_STATIC_LIBS ON)
endif()

find_package(CURL REQUIRED)

if(NOT CURL_FOUND)
    message(FATAL_ERROR
        "未找到 libcurl。请安装:\n"
        "  Windows (MSYS2): pacman -S mingw-w64-x86_64-curl\n"
        "  Windows (vcpkg):  vcpkg install curl\n"
        "  Ubuntu/Debian:    sudo apt install libcurl4-openssl-dev\n"
        "  Fedora:           sudo dnf install libcurl-devel\n"
        "  macOS:            brew install curl\n"
        "  或设置 CMAKE_PREFIX_PATH 指向 libcurl 安装目录")
endif()

message(STATUS "CURL 版本: ${CURL_VERSION}")
message(STATUS "CURL 库:   ${CURL_LIBRARIES}")
message(STATUS "CURL 头文件: ${CURL_INCLUDE_DIRS}")

# ---- 可执行文件 ----
add_executable(DeepseekTest
    src/main.cpp
)

target_link_libraries(DeepseekTest PRIVATE CURL::libcurl)

# ---- MSVC: UTF-8 源文件 + 静态链接 CRT (减小分发体积) ----
if(MSVC)
    target_compile_options(DeepseekTest PRIVATE /utf-8)
    # 静态链接 CRT，避免目标机器缺少 MSVC 运行时 DLL
    set_property(TARGET DeepseekTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# ---- MinGW: 静态链接 libgcc/libstdc++ (减少 DLL 依赖) ----
if(MINGW)
    target_link_options(DeepseekTest PRIVATE
        -static-libgcc -static-libstdc++)
endif()

# ============================================================================
# 安装规则: 把 exe/DLL 打包到 dist/ 目录
# ============================================================================
include(GNUInstallDirs)

install(TARGETS DeepseekTest
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# ---- Windows: 自动收集 libcurl 所需 DLL ----
if(WIN32 AND NOT DEEPSEEK_STATIC_LIBCURL)
    # 定义已知的 libcurl DLL 文件列表 (MinGW 常见依赖)
    set(DLL_SEARCH_DIRS
        "${CURL_LIBRARIES}/../bin"          # 从 .dll.a 推断 bin 目录
        "${CURL_INCLUDE_DIRS}/../bin"
        "C:/msys64/mingw64/bin"
        "C:/vcpkg/installed/x64-windows/bin"
    )

    # 获取 curl 库文件所在目录
    get_filename_component(CURL_LIB_DIR "${CURL_LIBRARIES}" DIRECTORY)
    get_filename_component(CURL_LIB_PARENT "${CURL_LIB_DIR}" DIRECTORY)
    if(EXISTS "${CURL_LIB_PARENT}/bin")
        list(APPEND DLL_SEARCH_DIRS "${CURL_LIB_PARENT}/bin")
    endif()

    message(STATUS "DLL 搜索路径: ${DLL_SEARCH_DIRS}")

    # 自定义函数: 查找并安装 DLL
    function(install_dll DLL_NAME)
        find_file(FOUND_${DLL_NAME}
            NAMES ${DLL_NAME}
            PATHS ${DLL_SEARCH_DIRS}
            NO_DEFAULT_PATH
        )
        if(FOUND_${DLL_NAME})
            install(FILES ${FOUND_${DLL_NAME}}
                DESTINATION ${CMAKE_INSTALL_BINDIR})
            message(STATUS "  [DLL] ${DLL_NAME} → ${FOUND_${DLL_NAME}}")
        else()
            message(WARNING "  [DLL] ${DLL_NAME} 未找到，分发时需手动添加")
        endif()
    endfunction()

    # libcurl 核心及其典型依赖链
    install_dll(libcurl-4.dll)
    install_dll(libcurl.dll)          # MSVC 命名

    # SSL/TLS 后端 (OpenSSL 或 GnuTLS，择一存在即可)
    install_dll(libssl-3-x64.dll)
    install_dll(libcrypto-3-x64.dll)
    install_dll(libgnutls-30.dll)
    install_dll(libssl-3.dll)         # 备选命名
    install_dll(libcrypto-3.dll)

    # HTTP/2 & HTTP/3
    install_dll(libnghttp2-14.dll)
    install_dll(libnghttp3-9.dll)
    install_dll(libngtcp2-16.dll)

    # 压缩
    install_dll(libbrotlidec.dll)
    install_dll(libbrotlicommon.dll)
    install_dll(libzstd.dll)
    install_dll(zlib1.dll)            # MSVC 命名

    # IDN / SSH
    install_dll(libidn2-0.dll)
    install_dll(libpsl-5.dll)
    install_dll(libssh2-1.dll)

    # 加密后端依赖 (GnuTLS 链)
    install_dll(libgmp-10.dll)
    install_dll(libhogweed-6.dll)
    install_dll(libnettle-8.dll)
    install_dll(libp11-kit-0.dll)
    install_dll(libffi-8.dll)
    install_dll(libtasn1-6.dll)
    install_dll(libunistring-5.dll)

    # 国际化
    install_dll(libiconv-2.dll)
    install_dll(libintl-8.dll)

    # ngTCP2 加密后端
    install_dll(libngtcp2_crypto_gnutls-8.dll)
    install_dll(libngtcp2_crypto_ossl-0.dll)

    # 动态 CRT (仅 MSVC 非静态模式)
    if(MSVC)
        install_dll(msvcp140.dll)
        install_dll(vcruntime140.dll)
    endif()
endif()

# ---- 安装文档 ----
install(FILES AGENTS.md README.md
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
    OPTIONAL
)
