cmake_minimum_required(VERSION 3.16)
project(movebeat VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(movebeat_core
    core/src/chart.cpp
    core/src/game.cpp
    core/src/level.cpp
    core/src/records.cpp
    core/src/settings.cpp
)
target_include_directories(movebeat_core PUBLIC core/include)

if(MSVC)
    target_compile_options(movebeat_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(movebeat_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(movebeat_demo desktop/main.cpp)
target_link_libraries(movebeat_demo PRIVATE movebeat_core)

# Compile every PS3 translation unit against a narrow API surface mirrored from
# current PSL1GHT. This catches ordinary C++ regressions on developer machines;
# the GitHub Actions PPU build remains the authoritative ABI/link check.
add_library(movebeat_ps3_syntax OBJECT
    ps3/main.cpp
    ps3/app.cpp
    ps3/draw_batch.cpp
    ps3/rsx_batch_renderer.cpp
    ps3/demo_options.cpp
    ps3/level_library.cpp
    ps3/playfield_draw.cpp
    ps3/screens_draw.cpp
    ps3/text_draw.cpp
    ps3/framebuffer.cpp
    ps3/library_card.cpp
    ps3/move_input.cpp
    ps3/demo_input.cpp
    ps3/gem_runtime.cpp
    ps3/audio_player.cpp
    ps3/run_diagnostics.cpp
)
# RSX-only code compiles to no-op stand-ins against these stubs.
target_compile_definitions(movebeat_ps3_syntax PRIVATE MOVEBEAT_PS3_STUBS=1)
target_include_directories(movebeat_ps3_syntax PRIVATE
    tests/ps3_stubs
    ps3
    core/include
)
if(MSVC)
    target_compile_options(movebeat_ps3_syntax PRIVATE /W4 /permissive-)
else()
    target_compile_options(movebeat_ps3_syntax PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()
add_executable(movebeat_tests tests/game_tests.cpp)
target_link_libraries(movebeat_tests PRIVATE movebeat_core)
add_test(NAME movebeat_core_tests COMMAND movebeat_tests)

add_executable(movebeat_move_input_tests
    tests/move_input_tests.cpp
    ps3/move_input.cpp
    ps3/demo_input.cpp
)
target_link_libraries(movebeat_move_input_tests PRIVATE movebeat_core)
target_include_directories(movebeat_move_input_tests PRIVATE
    tests/ps3_stubs
    ps3
    core/include
)
add_test(NAME movebeat_move_input_tests COMMAND movebeat_move_input_tests)

add_executable(movebeat_draw_batch_tests
    tests/draw_batch_tests.cpp
    ps3/draw_batch.cpp
)
target_include_directories(movebeat_draw_batch_tests PRIVATE ps3 core/include)
add_test(NAME movebeat_draw_batch_tests COMMAND movebeat_draw_batch_tests)

add_executable(movebeat_run_diagnostics_tests
    tests/run_diagnostics_tests.cpp
    ps3/run_diagnostics.cpp
)
target_include_directories(movebeat_run_diagnostics_tests PRIVATE ps3)
add_test(NAME movebeat_run_diagnostics_tests
    COMMAND movebeat_run_diagnostics_tests)

add_executable(movebeat_gem_runtime_tests
    tests/gem_runtime_tests.cpp
    ps3/gem_runtime.cpp
    ps3/move_input.cpp
)
target_include_directories(movebeat_gem_runtime_tests PRIVATE
    tests/ps3_stubs
    ps3
    core/include
)
add_test(NAME movebeat_gem_runtime_tests COMMAND movebeat_gem_runtime_tests)

find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    if(DEFINED ENV{ORG_JSON_JAR} AND EXISTS "$ENV{ORG_JSON_JAR}")
        add_test(
            NAME movebeat_android_parity_tests
            COMMAND ${Python3_EXECUTABLE} -m unittest tests.test_android_parity -v
        )
        set_tests_properties(movebeat_android_parity_tests PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            ENVIRONMENT "ORG_JSON_JAR=$ENV{ORG_JSON_JAR}"
        )
    endif()
    add_test(
        NAME movebeat_beatsaver_import_tests
        COMMAND ${Python3_EXECUTABLE} -m unittest tests.test_beatsaver_import -v
    )
    set_tests_properties(movebeat_beatsaver_import_tests PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
    add_test(
        NAME movebeat_library_card_tests
        COMMAND ${Python3_EXECUTABLE} -m unittest tests.test_library_card -v
    )
    set_tests_properties(movebeat_library_card_tests PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
    add_test(
        NAME movebeat_track_assistant_tests
        COMMAND ${Python3_EXECUTABLE} -m unittest tests.test_assistant -v
    )
    set_tests_properties(movebeat_track_assistant_tests PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
    add_test(
        NAME movebeat_ftp_upload_tests
        COMMAND ${Python3_EXECUTABLE} -m unittest tests.test_ftp_upload -v
    )
    set_tests_properties(movebeat_ftp_upload_tests PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
    add_test(
        NAME movebeat_gem_capture_analysis_tests
        COMMAND ${Python3_EXECUTABLE} -m unittest tests.test_gem_capture_analysis -v
    )
    set_tests_properties(movebeat_gem_capture_analysis_tests PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
    add_test(
        NAME movebeat_ps3_monitor_tests
        COMMAND ${Python3_EXECUTABLE} -m unittest tests.test_ps3_monitor -v
    )
    set_tests_properties(movebeat_ps3_monitor_tests PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
    add_test(
        NAME movebeat_ppm_to_bmp_tests
        COMMAND ${Python3_EXECUTABLE} -m unittest tests.test_ppm_to_bmp -v
    )
    set_tests_properties(movebeat_ppm_to_bmp_tests PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
endif()
