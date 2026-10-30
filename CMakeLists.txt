cmake_minimum_required(VERSION 3.20)
project(Dessa LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Lexer, parser, analyser and the code generators (C, and C++ for the few
# programs whose C fails to compile)
set(DESSA_FRONTEND
    src/lexer/lexer.cpp
    src/parser/parser.cpp
    src/analyser/analyser.cpp
    src/analyser/analyser_expr.cpp
    src/analyser/builtins.cpp
    src/analyser/scope.cpp
    src/analyser/semtype.cpp
    src/analyser/generics.cpp
    src/codegen/codegen.cpp
    src/codegen/cgen.cpp
    src/codegen/cgen_expr.cpp
    src/codegen/cglue.cpp
    src/codegen/cgen_web.cpp
    src/driver/loader.cpp
)
set(DESSA_INCLUDES src/common src/lexer src/parser src/analyser src/codegen src/driver)

add_executable(dessa src/main.cpp src/driver/toolchain.cpp src/driver/web.cpp ${DESSA_FRONTEND})
find_package(Threads REQUIRED)
target_link_libraries(dessa PRIVATE Threads::Threads)   # dessa serve reads keys on its own thread
if(WIN32)
    target_link_libraries(dessa PRIVATE ws2_32)   # dessa serve
endif()
target_include_directories(dessa PRIVATE ${DESSA_INCLUDES})

# Lets a freshly built compiler find runtime/ inside the source tree.
target_compile_definitions(dessa PRIVATE DESSA_RUNTIME_DIR="${CMAKE_SOURCE_DIR}/runtime")

if(MSVC)
    target_compile_options(dessa PRIVATE /W4)
else()
    target_compile_options(dessa PRIVATE -Wall -Wextra)
    if(WIN32)
        # Self-contained dessa.exe (no MinGW DLLs needed at runtime)
        target_link_options(dessa PRIVATE -static)
    elseif(NOT APPLE)
        # Prebuilt Linux binaries must not need a newer libstdc++ than the user's
        target_link_options(dessa PRIVATE -static-libstdc++ -static-libgcc)
    endif()
endif()

# riv, the package manager, is written in Dessa and compiled with dessa:
#   cmake --build build --target riv
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/riv${CMAKE_EXECUTABLE_SUFFIX}
    COMMAND ${CMAKE_COMMAND} -E env DESSA_PACKAGES=${CMAKE_SOURCE_DIR}/tools/riv/packages $<TARGET_FILE:dessa> build ${CMAKE_SOURCE_DIR}/tools/riv/riv.ds -o ${CMAKE_BINARY_DIR}/riv${CMAKE_EXECUTABLE_SUFFIX}
    DEPENDS dessa ${CMAKE_SOURCE_DIR}/tools/riv/riv.ds
    COMMENT "Building riv with dessa")
add_custom_target(riv DEPENDS ${CMAKE_BINARY_DIR}/riv${CMAKE_EXECUTABLE_SUFFIX})
