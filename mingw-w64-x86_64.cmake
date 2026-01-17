## CMake toolchain file for cross-compiling to Windows x86_64 with MinGW-w64
## Usage: cmake -DCMAKE_TOOLCHAIN_FILE=toolchains/mingw-w64-x86_64.cmake ...

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_VERSION 1)

# Cross compilers (installed from Arch: mingw-w64-gcc)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)

# Prefer finding libraries and headers in the target/sysroot
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# Default optimization flags for Release builds; users can override.
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(NOT CMAKE_C_FLAGS)
  set(CMAKE_C_FLAGS "-O2 -g" CACHE STRING "C flags" FORCE)
endif()

if(NOT CMAKE_CXX_FLAGS)
  set(CMAKE_CXX_FLAGS "-O2 -g" CACHE STRING "CXX flags" FORCE)
endif()

## Optionally force static CRT linking (uncomment to enable)
# set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static-libstdc++")
