cmake_minimum_required(VERSION 3.19) # libghostty-vt's CMake wrapper needs 3.19
project(cui-repl VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(WARNINGS -Wall -Wextra -Wshadow -Wstrict-prototypes)

find_package(Threads REQUIRED)

# libghostty-vt 1.0.0. Its wrapper runs `zig build`, so Zig 0.17 must be on PATH.
# Offline: -DFETCHCONTENT_SOURCE_DIR_GHOSTTY=/path/to/libghostty-vt-1.0.0
include(FetchContent)
FetchContent_Declare(ghostty
  URL https://release.files.ghostty.org/libghostty-vt/1.0.0/libghostty-vt-1.0.0.tar.gz
  URL_HASH SHA256=4a6ef758be721923e1969e7b81b443dcc92d8dd3fb5a39600c03a8817a753676
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(ghostty)

# The pty and the emulated session: no CUI, so the tests run headless.
add_library(replcore STATIC src/pty.c src/session.c)
target_include_directories(replcore PUBLIC src)
target_compile_options(replcore PRIVATE ${WARNINGS})
target_link_libraries(replcore PUBLIC ghostty-vt-static)

# CUI 1.3.1, with the accessors in cuix.c compiled into the same unit. On macOS
# its window layer is Objective-C, so the unit compiles as such.
add_library(cui STATIC src/cuix.c)
target_include_directories(cui PUBLIC vendor/cui src)
target_compile_options(cui PRIVATE -w)
if(APPLE)
  enable_language(OBJC)
  set_source_files_properties(src/cuix.c PROPERTIES LANGUAGE OBJC)
  target_compile_options(cui PRIVATE -fobjc-arc)
  target_compile_definitions(cui PRIVATE _DARWIN_C_SOURCE)
  target_link_libraries(cui PUBLIC "-framework Cocoa" "-framework QuartzCore" "-framework OpenGL")
elseif(UNIX)
  find_package(X11 REQUIRED)
  target_compile_definitions(cui PRIVATE _DEFAULT_SOURCE)
  target_link_libraries(cui PUBLIC X11::X11 ${CMAKE_DL_LIBS} m Threads::Threads)
endif()

# The terminal widget, shared by the app and its tests.
add_library(replview STATIC src/term_view.c src/boxdraw.c)
target_compile_options(replview PRIVATE ${WARNINGS})
target_link_libraries(replview PUBLIC replcore cui)

add_executable(cui-repl src/main.c)
target_compile_options(cui-repl PRIVATE ${WARNINGS})
target_link_libraries(cui-repl PRIVATE replview)
install(TARGETS cui-repl RUNTIME DESTINATION bin)

enable_testing()
foreach(t pty session)
  add_executable(test_${t} tests/test_${t}.c)
  target_compile_options(test_${t} PRIVATE ${WARNINGS})
  target_link_libraries(test_${t} PRIVATE replcore)
  add_test(NAME ${t} COMMAND test_${t})
endforeach()
add_executable(test_view tests/test_view.c)
target_compile_options(test_view PRIVATE ${WARNINGS})
target_link_libraries(test_view PRIVATE replview)
add_test(NAME view COMMAND test_view)

find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_test(NAME smoke COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke.py
           $<TARGET_FILE:cui-repl>)
  set_tests_properties(smoke PROPERTIES SKIP_RETURN_CODE 77)
endif()
