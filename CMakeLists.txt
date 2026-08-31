cmake_minimum_required(VERSION 3.25)

set(PROJECT_NAME_INPUT "Project" CACHE STRING "Project name")
set(PROJECT_DESCRIPTION_INPUT "C++ Project" CACHE STRING "Project description")
set(PROJECT_VERSION_INPUT "0.1.0" CACHE STRING "Project version")

project(${PROJECT_NAME_INPUT}
    VERSION ${PROJECT_VERSION_INPUT}
    DESCRIPTION "${PROJECT_DESCRIPTION_INPUT}"
    LANGUAGES CXX
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---- C++ standards ----
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- Sources ----
file(GLOB PROJECT_SOURCES CONFIGURE_DEPENDS
    src/*.cpp
)

# ---- Executable ----
add_executable(${PROJECT_NAME}
    ${PROJECT_SOURCES}
)

# ---- MSVC runtime ----
if(MSVC)
    set_property(TARGET ${PROJECT_NAME} PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
endif()

# ---- Compiler options ----
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE
        /W4
        /permissive-
    )
else()
    target_compile_options(${PROJECT_NAME} PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()

# ---- Output directory ----
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# ---- Install ----
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)

# ---- Configuration summary ----
message(STATUS "")
message(STATUS "${PROJECT_NAME} Configuration:")
message(STATUS "  Version:        ${PROJECT_VERSION}")
message(STATUS "  Description:    ${PROJECT_DESCRIPTION}")
message(STATUS "  C++ standard:   C++${CMAKE_CXX_STANDARD}")
message(STATUS "  Compiler:       ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "")