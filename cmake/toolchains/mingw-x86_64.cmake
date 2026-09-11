# Cross-compile Ring Out for 64-bit Windows from a Linux host.
#
# PROVENANCE. This file is adapted from the community fork at
# github.com/Ell/RingOut (GPL-2.0-or-later, same licence as this project),
# which reached a working Windows cross-build before this project did. The
# approach is theirs; the comments here are expanded from what their version
# had. Credited rather than reinvented.
#
# WHY THIS EXISTS AT ALL. The retired Windows workflow built on a windows-2022
# runner, which bills at 2x and cannot be reproduced on this project's own
# hardware -- there is no Windows machine here, so CI was the only feedback
# loop and every iteration cost a full remote round trip. Cross-compiling means
# a Windows binary can be produced on the Linux desktop that already exists,
# and the same toolchain file works unchanged in CI on a plain ubuntu runner.
#
# The compiler names can be overridden before including this file, which lets a
# caller use either distro MinGW-w64 GCC or an llvm-mingw toolchain on PATH.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc CACHE FILEPATH "MinGW C compiler")
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++ CACHE FILEPATH "MinGW C++ compiler")
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres CACHE FILEPATH "MinGW resource compiler")

# Windows' filesystem is case-insensitive, and Dolphin contains system-header
# includes written with the SDK's display casing (<SetupAPI.h>, <Windows.h>,
# <XInput.h> and so on). MinGW's Linux sysroot stores those headers lowercase,
# and a Linux filesystem is case-SENSITIVE, so every one of them fails to
# resolve. mingw-case-headers/ holds a one-line alias per name -- each is
# nothing but `#include <lowercase.h>` -- and putting that directory first on
# the include path makes the SDK casing resolve.
#
# This is the entire reason a cross-build appears not to work when first tried:
# the errors read as missing Windows SDK headers, which invites the conclusion
# that the sysroot is broken or incomplete. It is not. It is only the casing.
set(_ringout_mingw_case_headers
    "${CMAKE_CURRENT_LIST_DIR}/mingw-case-headers")
# Target Windows 10.
#
# MinGW's headers gate declarations on _WIN32_WINNT and default to a much older
# Windows than this project supports. That default is not a smaller API, it is a
# SILENT one: processthreadsapi.h declares SetProcessInformation only under
# `#if _WIN32_WINNT >= 0x0602`, so the call in Common/Timer.cpp fails to compile
# with "undeclared identifier" and reads like MinGW lacking the API. It does not
# lack it -- the same header has SetProcessInformation, ProcessPowerThrottling,
# PROCESS_POWER_THROTTLING_STATE and SetThreadDescription, all behind version
# gates.
#
# 0x0A00 is Windows 10, which is what the v143 MSVC toolset the retired
# workflow used compiles against, so this matches rather than changes the
# supported floor.
set(_ringout_win32_winnt "-D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00")

set(CMAKE_C_FLAGS_INIT "-I${_ringout_mingw_case_headers} ${_ringout_win32_winnt}")
set(CMAKE_CXX_FLAGS_INIT "-I${_ringout_mingw_case_headers} ${_ringout_win32_winnt}")

execute_process(
    COMMAND "${CMAKE_C_COMPILER}" -print-sysroot
    OUTPUT_VARIABLE _ringout_mingw_sysroot
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
if(NOT _ringout_mingw_sysroot)
    # Distro MinGW-w64 GCC commonly reports an empty sysroot while installing
    # its target headers and libraries in this conventional prefix.
    set(_ringout_mingw_sysroot "/usr/x86_64-w64-mingw32")
endif()
set(CMAKE_FIND_ROOT_PATH "${_ringout_mingw_sysroot}")

# Programs such as Python and Git run on the Linux build host. Headers,
# libraries, and CMake packages must come only from the Windows target root.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# pkg-config does not obey CMAKE_FIND_ROOT_PATH on its own. Without these
# settings it can inject /usr/include and Linux libraries into Windows compile
# commands, producing conflicts between glibc and the UCRT declarations.
set(ENV{PKG_CONFIG_PATH} "")
set(ENV{PKG_CONFIG_LIBDIR} "${_ringout_mingw_sysroot}/lib/pkgconfig")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "${_ringout_mingw_sysroot}")
set(PKG_CONFIG_USE_CMAKE_PREFIX_PATH OFF CACHE BOOL "" FORCE)
