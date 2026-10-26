cmake_minimum_required(VERSION 3.20)
project(Dessa LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Lexer, parser, analyser and both code generators: shared by the dessa
# command and the WebAssembly build.
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
    src/codegen/jsgen.cpp
    src/driver/loader.cpp
)
set(DESSA_INCLUDES src/common src/lexer src/parser src/analyser src/codegen src/driver)

if(EMSCRIPTEN)
    # ---- web/dessa.js: the compiler running in a browser ----
    #   emcmake cmake -S . -B build-wasm && cmake --build build-wasm
    add_executable(dessa_web src/web/wasm_main.cpp ${DESSA_FRONTEND})
    target_include_directories(dessa_web PRIVATE ${DESSA_INCLUDES})
    set_target_properties(dessa_web PROPERTIES OUTPUT_NAME "dessa" SUFFIX ".js"
                          RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/web)
    set(S ${CMAKE_SOURCE_DIR})
    set(P ${S}/tools/riv/packages)
    target_link_options(dessa_web PRIVATE
        -O2
        -sMODULARIZE=1 -sEXPORT_NAME=DessaCompiler
        -sEXPORTED_FUNCTIONS=_dessa_compile,_dessa_version
        -sEXPORTED_RUNTIME_METHODS=ccall,FS
        -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=4MB
        -sENVIRONMENT=web,node
        -sSINGLE_FILE=1
        # The core libraries, the runtime and the riv packages written in Dessa
        "SHELL:--embed-file ${S}/include@/dessa/include"
        "SHELL:--embed-file ${S}/runtime/dessa_runtime.js@/dessa/runtime/dessa_runtime.js"
        "SHELL:--embed-file ${P}/json@/dessa/packages/json"
        "SHELL:--embed-file ${P}/colors@/dessa/packages/colors"
        "SHELL:--embed-file ${P}/textfx@/dessa/packages/textfx"
        "SHELL:--embed-file ${P}/numbers@/dessa/packages/numbers"
        "SHELL:--embed-file ${P}/codes@/dessa/packages/codes"
        # The page loader (script type="text/dessa") goes after the module
        "SHELL:--extern-post-js ${S}/web/engine.js"
    )
    file(GLOB_RECURSE DESSA_EMBEDDED ${S}/include/*.dm ${S}/include/*.js ${P}/*.ds)
    set_property(TARGET dessa_web APPEND PROPERTY LINK_DEPENDS ${S}/web/engine.js ${S}/runtime/dessa_runtime.js ${DESSA_EMBEDDED})
    return()
endif()

add_executable(dessa src/main.cpp src/driver/toolchain.cpp src/driver/web.cpp ${DESSA_FRONTEND})
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
