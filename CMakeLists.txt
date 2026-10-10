cmake_minimum_required(VERSION 3.20)
project(shadowflare_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(SFE_FETCH_SDL "Download and build SDL2 if it is not installed" ON)

add_executable(shadowflare
    src/main.cpp
    src/core/log.cpp
    src/platform/filesystem.cpp
    src/game/game.cpp
)
target_include_directories(shadowflare PRIVATE src)

if(MSVC)
    target_compile_options(shadowflare PRIVATE /W4)
else()
    target_compile_options(shadowflare PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(EMSCRIPTEN)
    # Emscripten ships its own SDL2 port.
    target_compile_options(shadowflare PRIVATE -sUSE_SDL=2)
    target_link_options(shadowflare PRIVATE -sUSE_SDL=2 -sALLOW_MEMORY_GROWTH=1 -sFORCE_FILESYSTEM=1)
    set_target_properties(shadowflare PROPERTIES SUFFIX ".html")
else()
    find_package(SDL2 CONFIG QUIET)
    if(NOT SDL2_FOUND AND SFE_FETCH_SDL)
        include(FetchContent)
        FetchContent_Declare(SDL2
            GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
            GIT_TAG release-2.30.9
            GIT_SHALLOW TRUE)
        set(SDL_TEST OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(SDL2)
    elseif(NOT SDL2_FOUND)
        message(FATAL_ERROR "SDL2 not found. Install it or configure with -DSFE_FETCH_SDL=ON.")
    endif()
    if(TARGET SDL2::SDL2main)
        target_link_libraries(shadowflare PRIVATE SDL2::SDL2main)
    endif()
    target_link_libraries(shadowflare PRIVATE SDL2::SDL2)
endif()
