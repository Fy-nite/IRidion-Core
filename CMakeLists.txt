cmake_minimum_required(VERSION 3.16)
project(ObjectIR.CppRuntime VERSION 1.0.0 LANGUAGES CXX)

# Prefer newer policy behavior for robustness
# Prefer newer policy behavior for robustness
cmake_policy(SET CMP0135 NEW)

# Keep a minimum policy baseline for older bundled subprojects (e.g. json)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

# Basic project-wide settings
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(BUILD_GUI ON)

# Options
option(BUILD_EXAMPLES "Build example programs" ON)
option(BUILD_PLUGINS "Build example plugins" ON)
option(BUILD_STATIC_RUNTIME "Build the objectir_runtime library as a static library" OFF)
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
## This is useful when building a standalone OJRuntime executable that contains
## all runtime symbols and does not require a separate DLL on Windows.
set(OBJECTIR_RUNTIME_LINKAGE SHARED)
if(BUILD_STATIC_RUNTIME)
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
add_executable(OJRuntime src/main.cpp)
target_link_libraries(OJRuntime PRIVATE objectir_runtime)
target_include_directories(OJRuntime PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(BUILD_STATIC_RUNTIME)
    if(MINGW)
        # For MinGW builds, request static linking of libgcc and libstdc++ to minimize dll deps
        target_link_options(OJRuntime PRIVATE "-static-libgcc" "-static-libstdc++")
    elseif(MSVC)
        # For the MSVC toolchain prefer static CRT by setting the runtime library (MSVC >= 2019)
        set_target_properties(OJRuntime PROPERTIES
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
endif()

# Option to build GUI frontend
option(BUILD_GUI "Build ObjectIR GUI frontend with Qt" OFF)
option(QT_FORCE_NO_VULKAN "Temporarily unset VULKAN_SDK when finding Qt to avoid FindVulkan errors (MSYS/Mingw)" ON)
option(QT_PREFER_QT5 "Prefer Qt5 over Qt6 when both are available" OFF)

# Build GUI frontend if enabled and Qt is available
if(BUILD_GUI)
    # Find Qt only when GUI is requested
    set(Qt_FOUND FALSE)

    # Optionally unset VULKAN_SDK on MSYS/Mingw to avoid Qt6 FindVulkan probing errors
    if(QT_FORCE_NO_VULKAN AND (MSYS OR MINGW OR WIN32))
      if(DEFINED ENV{VULKAN_SDK} AND NOT "$ENV{VULKAN_SDK}" STREQUAL "")
        message(STATUS "Temporarily unsetting VULKAN_SDK to avoid FindVulkan errors")
        set(ENV{VULKAN_SDK} "")
      endif()
    endif()

    if(QT_PREFER_QT5)
        find_package(Qt5 COMPONENTS Widgets Core QUIET)
        if(Qt5_FOUND)
            set(Qt_LIBS Qt5::Widgets Qt5::Core)
            set(Qt_VERSION_MAJOR 5)
            set(Qt_FOUND TRUE)
        else()
            find_package(Qt6 COMPONENTS Widgets Core QUIET)
            if(Qt6_FOUND)
                set(Qt_LIBS Qt6::Widgets Qt6::Core)
                set(Qt_VERSION_MAJOR 6)
                set(Qt_FOUND TRUE)
            endif()
        endif()
    else()
        find_package(Qt6 COMPONENTS Widgets Core QUIET)
        if(Qt6_FOUND)
            set(Qt_LIBS Qt6::Widgets Qt6::Core)
            set(Qt_VERSION_MAJOR 6)
            set(Qt_FOUND TRUE)
        else()
            find_package(Qt5 COMPONENTS Widgets Core QUIET)
            if(Qt5_FOUND)
                set(Qt_LIBS Qt5::Widgets Qt5::Core)
                set(Qt_VERSION_MAJOR 5)
                set(Qt_FOUND TRUE)
            endif()
        endif()
    endif()

    if(Qt_FOUND)
        set(CMAKE_AUTOMOC ON)
        set(CMAKE_AUTORCC ON)
        set(CMAKE_AUTOUIC ON)
        add_executable(objectir_gui src/gui_frontend.cpp)
        target_link_libraries(objectir_gui PRIVATE objectir_runtime ${Qt_LIBS})
        target_include_directories(objectir_gui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
        message(STATUS "ObjectIR GUI enabled (using Qt${Qt_VERSION_MAJOR})")
    else()
        message(WARNING "BUILD_GUI=ON but Qt6 or Qt5 not found. GUI will not be built.")
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
