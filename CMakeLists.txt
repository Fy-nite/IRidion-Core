cmake_minimum_required(VERSION 3.16)
project(ObjectIR.CppRuntime VERSION 1.0.0 LANGUAGES CXX)

# Prefer newer policy behavior for robustness
# Prefer newer policy behavior for robustness
cmake_policy(SET CMP0135 NEW)

# Keep a minimum policy baseline for older bundled subprojects (e.g. json)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

# Basic project-wide settings
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(BUILD_GUI OFF)

# Options
option(BUILD_EXAMPLES "Build example programs" OFF)
option(BUILD_PLUGINS "Build example plugins" OFF)
option(BUILD_STATIC_RUNTIME "Build the objectir_runtime library as a static library" OFF)
option(BUILD_STATIC_EXECUTABLE "Build the crystal executable statically with all libs embedded (platform-dependent)" OFF)
# Find nlohmann/json - try system first, then fetch
find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
        include(FetchContent)
        # Prefer timestamps based on extraction to avoid stale builds
        set(FETCHCONTENT_QUIET OFF)
        FetchContent_Declare(
            json
            URL https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
            TLS_VERIFY OFF
        )
        FetchContent_MakeAvailable(json)
endif()

# Qt will be detected only when GUI build is requested (below)

# Add the library
## Provide an option to build the runtime as a static library instead of a shared one.
## This is useful when building a standalone crystal executable that contains
## all runtime symbols and does not require a separate DLL on Windows.
set(OBJECTIR_RUNTIME_LINKAGE SHARED)
# If either the library or the executable is requested to be fully static,
# prefer building the runtime library as a static library so it can be
# embedded into the standalone executable.
if(BUILD_STATIC_RUNTIME OR BUILD_STATIC_EXECUTABLE)
    set(OBJECTIR_RUNTIME_LINKAGE STATIC)
endif()

add_library(objectir_runtime ${OBJECTIR_RUNTIME_LINKAGE}
    src/objectir_runtime.cpp
    src/DiagnosticsProvidr.cpp
    src/ir_loader.cpp
    src/ir_text_parser.cpp
    src/fob_loader.cpp
    src/instruction_executor.cpp
    src/objectir_plugin_api.cpp
    src/stdlib.cpp
    src/runtime_c_api.cpp
)

# Public include directory
target_include_directories(objectir_runtime PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Use target-based C++ standard requirement
target_compile_features(objectir_runtime PUBLIC cxx_std_17)

# Compiler warning defaults
target_compile_options(objectir_runtime PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-Wall;-Wextra;-Wshadow>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

# Position independent for static/shared consistency
set_target_properties(objectir_runtime PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Link nlohmann/json
target_link_libraries(objectir_runtime PRIVATE nlohmann_json::nlohmann_json)

# Expose nlohmann_json include directories to consumers without exporting its target
target_include_directories(objectir_runtime PUBLIC
    $<BUILD_INTERFACE:$<TARGET_PROPERTY:nlohmann_json::nlohmann_json,INTERFACE_INCLUDE_DIRECTORIES>>
)

if(UNIX AND NOT APPLE AND NOT WIN32 AND NOT MINGW AND NOT MSYS)
    target_link_libraries(objectir_runtime PRIVATE dl)
endif()
# target_link_options(objectir_runtime PUBLIC "-lraylib")
if(OBJECTIR_RUNTIME_LINKAGE STREQUAL "STATIC")
    # For static runtime builds, instruct the compiler to not use dllexport/dllimport
    target_compile_definitions(objectir_runtime PUBLIC OBJECTIR_RUNTIME_STATIC)
endif()

# Set library properties
set_target_properties(objectir_runtime PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
)

# Build standalone executable
add_executable(crystal src/main.cpp)
target_link_libraries(crystal PRIVATE objectir_runtime)
target_include_directories(crystal PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(BUILD_STATIC_RUNTIME)
    if(MINGW)
        # For MinGW builds, request static linking of libgcc and libstdc++ to minimize dll deps
        target_link_options(crystal PRIVATE "-static-libgcc" "-static-libstdc++")
    elseif(MSVC)
        # For the MSVC toolchain prefer static CRT by setting the runtime library (MSVC >= 2019)
        set_target_properties(crystal PROPERTIES
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
endif()

# When a fully static executable is requested, add platform specific linker
# flags to request static linking of the C/C++ runtime and other system
# libraries. Note: producing a fully static binary is platform-dependent and
# may require installing static versions of system libraries (libc, libm, etc.).
if(BUILD_STATIC_EXECUTABLE)
    if(MSVC)
        # MSVC: prefer static CRT
        set_target_properties(crystal PROPERTIES
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    elseif(MINGW)
        # MinGW: request static linking of libgcc/libstdc++ and pass -static
        target_link_options(crystal PRIVATE "-static" "-static-libgcc" "-static-libstdc++")
    else()
        # Generic UNIX/GNU toolchains: attempt fully static link with -static.
        # This will succeed only if all required static libraries are available
        # on the system. Users may need to install libc.a, libstdc++.a, etc.
        target_link_options(crystal PRIVATE "-static")
    endif()
    # When building a static executable, also ensure the runtime library is static
    # so its symbols are embedded directly into the executable.
    if(NOT OBJECTIR_RUNTIME_LINKAGE STREQUAL "STATIC")
        message(STATUS "Forcing objectir_runtime to STATIC because BUILD_STATIC_EXECUTABLE is set")
        set_target_properties(objectir_runtime PROPERTIES TYPE STATIC)
    endif()
endif()

# Option to build GUI frontend

option(BUILD_GUI "Build ObjectIR GUI frontend with Qt" OFF)
option(QT_FORCE_NO_VULKAN "Temporarily unset VULKAN_SDK when finding Qt to avoid FindVulkan errors (MSYS/Mingw)" ON)

# When cross-compiling we usually don't have a target Qt available on the host.
# If cross-compiling and no Qt search hints are provided, disable GUI to avoid
# CMake finding a host Qt installation that cannot be used for the target.
if(CMAKE_CROSSCOMPILING AND BUILD_GUI)
    if(NOT DEFINED Qt_DIR AND NOT DEFINED CMAKE_PREFIX_PATH)
        message(WARNING "Cross-compiling detected and no Qt target path provided; disabling BUILD_GUI.")
        set(BUILD_GUI OFF CACHE BOOL "Build ObjectIR GUI frontend with Qt" FORCE)
    else()
        message(STATUS "Cross-compiling with Qt search paths provided; CMake will attempt to find target Qt.")
    endif()
endif()

# Build GUI frontend if enabled and Qt is available
if(BUILD_GUI)
    # Find Qt6 only; this project now requires Qt6 for the GUI frontend.
    set(Qt_FOUND FALSE)

    # Optionally unset VULKAN_SDK on MSYS/Mingw to avoid Qt6 FindVulkan probing errors
    if(QT_FORCE_NO_VULKAN AND (MSYS OR MINGW OR WIN32))
      if(DEFINED ENV{VULKAN_SDK} AND NOT "$ENV{VULKAN_SDK}" STREQUAL "")
        message(STATUS "Temporarily unsetting VULKAN_SDK to avoid FindVulkan errors")
        set(ENV{VULKAN_SDK} "")
      endif()
    endif()

    find_package(Qt6 COMPONENTS Widgets Core QUIET)
    if(Qt6_FOUND)
        set(Qt_LIBS Qt6::Widgets Qt6::Core)
        set(Qt_VERSION_MAJOR 6)
        set(Qt_FOUND TRUE)
    endif()

    if(Qt_FOUND)
 
        add_executable(objectir_gui src/gui_frontend.cpp)
        target_link_libraries(objectir_gui PRIVATE objectir_runtime ${Qt_LIBS})
        target_include_directories(objectir_gui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
        message(STATUS "ObjectIR GUI enabled (using Qt6)")
    else()
        message(WARNING "BUILD_GUI=ON but Qt6 not found. GUI will not be built.")
    endif()
endif()

# Enable testing
enable_testing()

# Install and packaging helpers
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# Add example executables
if(BUILD_EXAMPLES)
    add_executable(calculator_example examples/calculator_example.cpp)
    target_link_libraries(calculator_example PRIVATE objectir_runtime)

    add_executable(todoapp_example examples/todoapp_example.cpp)
    target_link_libraries(todoapp_example PRIVATE objectir_runtime)

    add_executable(pipeline_example examples/pipeline_example.cpp)
    target_link_libraries(pipeline_example PRIVATE objectir_runtime)

    add_executable(fob_test examples/fob_test.cpp)
    target_link_libraries(fob_test PRIVATE objectir_runtime)

    add_executable(format_test examples/format_test.cpp)
    target_link_libraries(format_test PRIVATE objectir_runtime)

    add_executable(full_feature_suite examples/full_feature_suite.cpp)
    target_link_libraries(full_feature_suite PRIVATE objectir_runtime)
endif()

# Example plugin (shared library)
if(BUILD_PLUGINS)
    add_library(objectir_example_override_plugin SHARED
            plugins/example_override_plugin.cpp
    )
    target_link_libraries(objectir_example_override_plugin PRIVATE objectir_runtime)
    target_include_directories(objectir_example_override_plugin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
endif()


# Install configuration
# Only install/export package when this project is the top-level project.
# When included as a subdirectory of a larger project the top-level CMake
# generator is responsible for installation/exporting; exporting here can
# trigger missing-dependency errors for targets brought in via FetchContent.
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR AND NOT BUILD_STATIC_EXECUTABLE)
    install(TARGETS objectir_runtime
            EXPORT ObjectIRTargets
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

    # Export the targets for use by other projects
    install(EXPORT ObjectIRTargets FILE ObjectIRTargets.cmake NAMESPACE ObjectIR:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ObjectIR)

    # Create and install CMake package config
    configure_package_config_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ObjectIRConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/ObjectIRConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ObjectIR
    )

    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/ObjectIRConfigVersion.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY AnyNewerVersion
    )

    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/ObjectIRConfig.cmake ${CMAKE_CURRENT_BINARY_DIR}/ObjectIRConfigVersion.cmake DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ObjectIR)
endif()
