cmake_minimum_required(VERSION 3.16)
project(ObjectIR.CppRuntime VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(BUILD_GUI ON)
# Find nlohmann/json - try system first, then fetch
find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
    # Fetch nlohmann/json from GitHub if not already available
    include(FetchContent)
    FetchContent_Declare(json
      URL https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz
      TLS_VERIFY OFF
    )
    set(FETCHCONTENT_TLS_VERIFY OFF)
    FetchContent_MakeAvailable(json)
endif()

# Find Qt6 (or Qt5 as fallback)
find_package(Qt6 COMPONENTS Widgets QUIET)
if(NOT Qt6_FOUND)
    find_package(Qt5 COMPONENTS Widgets QUIET)
endif()

# Add the library
## Provide an option to build the runtime as a static library instead of a shared one.
## This is useful when building a standalone OJRuntime executable that contains
## all runtime symbols and does not require a separate DLL on Windows.
option(BUILD_STATIC_RUNTIME "Build the objectir_runtime library as a static library" OFF)

set(OBJECTIR_RUNTIME_LINKAGE SHARED)
if(BUILD_STATIC_RUNTIME)
    set(OBJECTIR_RUNTIME_LINKAGE STATIC)
endif()

add_library(objectir_runtime ${OBJECTIR_RUNTIME_LINKAGE}
    src/objectir_runtime.cpp
    src/ir_loader.cpp
    src/ir_text_parser.cpp
    src/fob_loader.cpp
    src/instruction_executor.cpp
    src/stdlib.cpp
    src/runtime_c_api.cpp
)

# Public include directory
target_include_directories(objectir_runtime PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Link nlohmann/json
target_link_libraries(objectir_runtime PUBLIC nlohmann_json::nlohmann_json)

if(OBJECTIR_RUNTIME_LINKAGE STREQUAL "STATIC")
    # For static runtime builds, instruct the compiler to not use dllexport/dllimport
    target_compile_definitions(objectir_runtime PUBLIC OBJECTIR_RUNTIME_STATIC)
endif()

# Set library properties
set_target_properties(objectir_runtime PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
)

# Option to build standalone executable
# Standalone builds should optionally use the static runtime library to avoid requiring
# the libobjectir_runtime DLL to be present next to the executable. When BUILD_STATIC_RUNTIME
# is ON, the static library is linked into the executable and the EXE will not import
# libobjectir_runtime.dll at runtime.
#
# On Windows (MinGW) you may also want to add -static-libgcc -static-libstdc++ when linking
# the executable to avoid depending on libstdc++-6.dll and libgcc DLLs. With MSVC, set the
# runtime library to /MT if a fully static CRT is desired.

# Build standalone executable if enabled
if(BUILD_STANDALONE)
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
endif()

# Option to build GUI frontend
option(BUILD_GUI "Build ObjectIR GUI frontend with Qt" OFF)

# Build GUI frontend if enabled and Qt is available
if(BUILD_GUI)
    # Enable MOC (Meta-Object Compiler) for Qt
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTORCC ON)
    set(CMAKE_AUTOUIC ON)
    

        set(Qt_VERSION_MAJOR 6)
        set(Qt_LIBS Qt6::Widgets Qt6::Core)

    add_executable(objectir_gui src/gui_frontend.cpp)
    target_link_libraries(objectir_gui PRIVATE objectir_runtime ${Qt_LIBS})
    target_include_directories(objectir_gui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    
    message(STATUS "ObjectIR GUI enabled (using Qt${Qt_VERSION_MAJOR})")
elseif(BUILD_GUI)
    message(WARNING "BUILD_GUI=ON but Qt6 and Qt5 not found. GUI will not be built.")
endif()

# Enable testing
enable_testing()

# Add example executables
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


# Install configuration
install(TARGETS objectir_runtime
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(DIRECTORY include/ DESTINATION include)
