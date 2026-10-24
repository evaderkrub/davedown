cmake_minimum_required(VERSION 3.24)

# Static CRT so the staged folder runs on a machine without the VC++
# redistributable. CMP0091 must be NEW (it is, with 3.24) for this to reach
# the FetchContent subprojects too.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

project(davedown VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# Everything the application needs at runtime is staged here, beside the
# executable, so the folder can be copied as a unit.
set(DAVEDOWN_STAGE_DIR "${CMAKE_BINARY_DIR}/stage")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${DAVEDOWN_STAGE_DIR}")

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
include(FetchContent)

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-3.4.8
    GIT_SHALLOW    TRUE)

FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.92.9-docking
    GIT_SHALLOW    TRUE)

FetchContent_Declare(imgui_test_engine
    GIT_REPOSITORY https://github.com/ocornut/imgui_test_engine.git
    GIT_TAG        v1.92.9
    GIT_SHALLOW    TRUE)

# stb_image for the pictures a markdown file references. Header only; pinned
# to a commit because the repository has no release tags.
FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        003501f6d7a8668ce8b6d875e34dd97ad0d5d5b5)

FetchContent_MakeAvailable(SDL3 imgui imgui_test_engine stb)

# --- Dear ImGui (docking) + SDL3 / SDL_Renderer backends -------------------
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp)
target_include_directories(imgui SYSTEM PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${imgui_SOURCE_DIR}/misc/cpp)
# The test engine hooks into ImGui through imconfig; every TU that includes
# imgui.h must see the same definitions or the ImGuiContext layout differs.
target_compile_definitions(imgui PUBLIC
    IMGUI_ENABLE_TEST_ENGINE
    IMGUI_TEST_ENGINE_ENABLE_COROUTINE_STDTHREAD_IMPL=1)
target_link_libraries(imgui PUBLIC SDL3::SDL3)

set(IMTE_DIR ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine)
add_library(imgui_test_engine STATIC
    ${IMTE_DIR}/imgui_te_engine.cpp
    ${IMTE_DIR}/imgui_te_context.cpp
    ${IMTE_DIR}/imgui_te_coroutine.cpp
    ${IMTE_DIR}/imgui_te_exporters.cpp
    ${IMTE_DIR}/imgui_te_perftool.cpp
    ${IMTE_DIR}/imgui_te_ui.cpp
    ${IMTE_DIR}/imgui_te_utils.cpp
    ${IMTE_DIR}/imgui_capture_tool.cpp)
target_include_directories(imgui_test_engine SYSTEM PUBLIC ${imgui_test_engine_SOURCE_DIR})
target_link_libraries(imgui_test_engine PUBLIC imgui)
# The engine's own imconfig additions (ImGuiTestEngineHook_* declarations)
# have to be visible to imgui.cpp as well.
target_compile_definitions(imgui PUBLIC IMGUI_USER_CONFIG="imgui_test_engine/imgui_te_imconfig.h")
target_include_directories(imgui SYSTEM PUBLIC ${imgui_test_engine_SOURCE_DIR})

# --- md4c (C) ----------------------------------------------------------------
add_library(md4c STATIC third_party/md4c/md4c.c)
target_include_directories(md4c SYSTEM PUBLIC third_party/md4c)
if(MSVC)
    target_compile_options(md4c PRIVATE /wd4244 /wd4267)
endif()

add_library(stb_image INTERFACE)
target_include_directories(stb_image SYSTEM INTERFACE ${stb_SOURCE_DIR})

# ---------------------------------------------------------------------------
# Project-wide warning policy for our own code only.
# ---------------------------------------------------------------------------
add_library(davedown_warnings INTERFACE)
if(MSVC)
    target_compile_options(davedown_warnings INTERFACE /W4 /permissive- /WX /utf-8 /EHsc
        /external:W0 /external:anglebrackets)
    target_compile_definitions(davedown_warnings INTERFACE _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(davedown_warnings INTERFACE -Wall -Wextra -Wpedantic -Werror)
endif()
target_compile_definitions(davedown_warnings INTERFACE DAVEDOWN_VERSION="${PROJECT_VERSION}")

# --- app: state and logic, no ImGui ------------------------------------------
add_library(davedown_app STATIC
    src/app/Document.cpp
    src/app/MarkdownModel.cpp
    src/app/Outline.cpp
    src/app/Settings.cpp
    src/app/Workspace.cpp)
target_include_directories(davedown_app PUBLIC src)
target_link_libraries(davedown_app PUBLIC davedown_warnings)

# --- platform: anything that touches the OS ----------------------------------
add_library(davedown_platform STATIC
    src/platform/Paths.cpp
    src/platform/FileDialog.cpp
    src/platform/Shell.cpp
    src/platform/Textures.cpp)
target_include_directories(davedown_platform PUBLIC src)
target_link_libraries(davedown_platform PUBLIC davedown_warnings davedown_app SDL3::SDL3 stb_image)

# --- ui: everything that draws -----------------------------------------------
add_library(davedown_ui STATIC
    src/ui/Theme.cpp
    src/ui/Fonts.cpp
    src/ui/Widgets.cpp
    src/ui/TextArea.cpp
    src/ui/MarkdownView.cpp
    src/ui/SourceEditor.cpp
    src/ui/WysiwygEditor.cpp
    src/ui/MainWindow.cpp)
target_include_directories(davedown_ui PUBLIC src third_party)
target_link_libraries(davedown_ui PUBLIC davedown_warnings davedown_app davedown_platform imgui md4c)

# --- the application ---------------------------------------------------------
add_executable(davedown WIN32 src/main.cpp)
if(WIN32)
    target_sources(davedown PRIVATE assets/app.rc)
endif()
target_link_libraries(davedown PRIVATE davedown_ui imgui_test_engine)
set_target_properties(davedown PROPERTIES OUTPUT_NAME "davedown")

# Stage runtime assets beside the executable.
add_custom_command(TARGET davedown POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            "${CMAKE_SOURCE_DIR}/assets/fonts" "${DAVEDOWN_STAGE_DIR}/assets/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            "${CMAKE_SOURCE_DIR}/assets/docs" "${DAVEDOWN_STAGE_DIR}/assets/docs"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/README.md" "${DAVEDOWN_STAGE_DIR}/README.md"
    COMMENT "Staging runtime assets")

# ---------------------------------------------------------------------------
# Tests: console binaries
# ---------------------------------------------------------------------------
enable_testing()

add_executable(davedown_unit_tests tests/unit_tests.cpp)
target_link_libraries(davedown_unit_tests PRIVATE davedown_app)
add_test(NAME unit_tests COMMAND davedown_unit_tests)

add_executable(davedown_e2e tests/e2e_main.cpp tests/e2e_tests.cpp)
target_link_libraries(davedown_e2e PRIVATE davedown_ui imgui_test_engine)
add_dependencies(davedown_e2e davedown)
add_test(NAME e2e COMMAND davedown_e2e)
