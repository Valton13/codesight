cmake_minimum_required(VERSION 3.10)

project(codesight VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)
find_package(Protobuf REQUIRED)
find_package(gRPC REQUIRED)

# Tree-sitter core library
add_library(tree-sitter STATIC
    third_party/tree-sitter/lib/src/lib.c
)

target_include_directories(tree-sitter PUBLIC
    third_party/tree-sitter/lib/include
    third_party/tree-sitter/lib/src
)

# Tree-sitter Python grammar
add_library(tree-sitter-python STATIC
    third_party/tree-sitter-python/src/parser.c
    third_party/tree-sitter-python/src/scanner.c
)

target_include_directories(tree-sitter-python PUBLIC
    third_party/tree-sitter-python/src
)

# Tree-sitter JavaScript grammar
add_library(tree-sitter-javascript STATIC
    third_party/tree-sitter-javascript/src/parser.c
    third_party/tree-sitter-javascript/src/scanner.c
)

target_include_directories(tree-sitter-javascript PUBLIC
    third_party/tree-sitter-javascript/src
)

# Generate protobuf and gRPC code
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

# Main executable sources
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
    tree-sitter
    tree-sitter-python
    tree-sitter-javascript
    gRPC::grpc++
    protobuf::libprotobuf
)

target_compile_options(codesight PRIVATE
    $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra>
)
