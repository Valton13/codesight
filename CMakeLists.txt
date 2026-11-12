cmake_minimum_required(VERSION 3.10)

project(codesight VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)
find_package(Protobuf REQUIRED)
find_package(gRPC REQUIRED)

# ---------------------------------------------------------------------------
# tree-sitter core
# ---------------------------------------------------------------------------
add_library(tree-sitter STATIC
    third_party/tree-sitter/lib/src/lib.c
)
target_include_directories(tree-sitter PUBLIC
    third_party/tree-sitter/lib/include
    third_party/tree-sitter/lib/src
)

# ---------------------------------------------------------------------------
# Grammar helper: builds one grammar library from a directory containing
# parser.c (+ optional scanner.c / scanner.cc).
# ---------------------------------------------------------------------------
function(add_ts_grammar TARGET DIR)
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/${DIR}/parser.c")
        message(FATAL_ERROR "Grammar missing: ${DIR}. Run scripts/fetch_grammars.sh")
    endif()

    add_library(${TARGET} STATIC "${DIR}/parser.c")

    if(EXISTS "${CMAKE_SOURCE_DIR}/${DIR}/scanner.c")
        target_sources(${TARGET} PRIVATE "${DIR}/scanner.c")
    elseif(EXISTS "${CMAKE_SOURCE_DIR}/${DIR}/scanner.cc")
        target_sources(${TARGET} PRIVATE "${DIR}/scanner.cc")
    endif()

    target_include_directories(${TARGET} PUBLIC "${DIR}")
    target_link_libraries(${TARGET} PUBLIC tree-sitter)
endfunction()

add_ts_grammar(ts-python      third_party/tree-sitter-python/src)
add_ts_grammar(ts-javascript  third_party/tree-sitter-javascript/src)
add_ts_grammar(ts-typescript  third_party/tree-sitter-typescript/typescript/src)
add_ts_grammar(ts-tsx         third_party/tree-sitter-typescript/tsx/src)
add_ts_grammar(ts-c           third_party/tree-sitter-c/src)
add_ts_grammar(ts-cpp         third_party/tree-sitter-cpp/src)
add_ts_grammar(ts-go          third_party/tree-sitter-go/src)
add_ts_grammar(ts-java        third_party/tree-sitter-java/src)
add_ts_grammar(ts-csharp      third_party/tree-sitter-c-sharp/src)
add_ts_grammar(ts-rust        third_party/tree-sitter-rust/src)
add_ts_grammar(ts-ruby        third_party/tree-sitter-ruby/src)
add_ts_grammar(ts-php         third_party/tree-sitter-php/php/src)

set(TS_GRAMMARS
    ts-python ts-javascript ts-typescript ts-tsx
    ts-c ts-cpp ts-go ts-java ts-csharp ts-rust ts-ruby ts-php
)

# ---------------------------------------------------------------------------
# Protobuf / gRPC codegen
# ---------------------------------------------------------------------------
set(PROTO_FILE "${CMAKE_SOURCE_DIR}/python/proto/codesight.proto")
set(PROTO_SRC "${CMAKE_BINARY_DIR}/codesight.pb.cc")
set(PROTO_HDR "${CMAKE_BINARY_DIR}/codesight.pb.h")
set(GRPC_SRC "${CMAKE_BINARY_DIR}/codesight.grpc.pb.cc")
set(GRPC_HDR "${CMAKE_BINARY_DIR}/codesight.grpc.pb.h")

add_custom_command(
    OUTPUT ${PROTO_SRC} ${PROTO_HDR} ${GRPC_SRC} ${GRPC_HDR}
    COMMAND protobuf::protoc
    ARGS --grpc_out=${CMAKE_BINARY_DIR}
         --cpp_out=${CMAKE_BINARY_DIR}
         -I${CMAKE_SOURCE_DIR}/python/proto
         --plugin=protoc-gen-grpc=`which grpc_cpp_plugin`
         ${PROTO_FILE}
    DEPENDS ${PROTO_FILE}
    COMMENT "Generating protobuf and gRPC code"
)

# ---------------------------------------------------------------------------
# Executable
# ---------------------------------------------------------------------------
set(SOURCES
    src/main.cpp
    src/utils/file_filter.cpp
    src/parser/parser.cpp
    src/ipc/grpc_client.cpp
    ${PROTO_SRC}
    ${GRPC_SRC}
)

if(UNIX AND NOT APPLE)
    list(APPEND SOURCES src/watcher/watcher_linux.cpp)
else()
    message(FATAL_ERROR "Only Linux is supported in this version.")
endif()

add_executable(codesight ${SOURCES})

target_include_directories(codesight PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
)

target_link_libraries(codesight PRIVATE
    Threads::Threads
    ${TS_GRAMMARS}
    gRPC::grpc++
    protobuf::libprotobuf
)

target_compile_options(codesight PRIVATE
    $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra>
)
