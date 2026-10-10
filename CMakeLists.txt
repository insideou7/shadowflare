cmake_minimum_required(VERSION 3.20)
project(shadowflare_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(SFE_FETCH_SDL "Download and build SDL2 if it is not installed" ON)

# Data file readers: no SDL dependency, so they can be tested on their own.
add_library(sf_formats STATIC
    src/formats/rclib.cpp
    src/formats/sprite_bank.cpp
    src/formats/map_layers.cpp
    src/formats/animation.cpp
    src/formats/scenario.cpp
    src/formats/scenario_script.cpp
    src/formats/table_data.cpp
)
target_include_directories(sf_formats PUBLIC src)

add_executable(shadowflare
    src/main.cpp
    src/core/log.cpp
    src/platform/filesystem.cpp
    src/render/sprite_cache.cpp
    src/game/map.cpp
    src/game/character.cpp
    src/game/game.cpp
)
target_link_libraries(shadowflare PRIVATE sf_formats)

foreach(target sf_formats shadowflare)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()

include(CTest)
if(BUILD_TESTING)
    add_executable(formats_test tests/formats_test.cpp)
    target_link_libraries(formats_test PRIVATE sf_formats)
    add_test(NAME formats COMMAND formats_test)
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
