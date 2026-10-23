# CMakeLists.txt — libFuzzer harnesses for MindCopy parsers

cmake_minimum_required(VERSION 3.16)

# libFuzzer is available with clang >= 6.0
# Bypass CMake detection which fails on link checks for fuzzer flags
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(WARNING "libFuzzer requires clang. Use: cmake -DCMAKE_CXX_COMPILER=clang++ ..")
    return()
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Source paths relative to this CMakeLists.txt (StudioTestBench/fuzz/)
set(STUDIO_SRC "${CMAKE_SOURCE_DIR}/../../BioNeuronStudio/src")
set(CORE_INC   "${CMAKE_SOURCE_DIR}/../../BioNeuronCore/include")
set(CORE_SRC   "${CMAKE_SOURCE_DIR}/../../BioNeuronCore/src")

set(FUZZ_FLAGS -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)
set(OBJ_FLAGS -fno-omit-frame-pointer -Wno-unused-result)

# ── fuzz_mcp_json : MCP JSON-RPC parser ─────────────────────────────
add_executable(fuzz_mcp_json fuzz_mcp_json.cpp)
target_include_directories(fuzz_mcp_json PRIVATE "${STUDIO_SRC}")
target_compile_options(fuzz_mcp_json PRIVATE ${FUZZ_FLAGS})
target_link_options(fuzz_mcp_json PRIVATE ${FUZZ_FLAGS})

# ── fuzz_plugin_desc : .desc JSON parser ────────────────────────────
add_executable(fuzz_plugin_desc fuzz_plugin_desc.cpp)
target_include_directories(fuzz_plugin_desc PRIVATE "${CORE_INC}")
target_compile_options(fuzz_plugin_desc PRIVATE ${FUZZ_FLAGS})
target_link_options(fuzz_plugin_desc PRIVATE ${FUZZ_FLAGS})

# ── fuzz_ipc_frame : IPC message frame parser ───────────────────────
add_executable(fuzz_ipc_frame fuzz_ipc_frame.cpp)
target_compile_options(fuzz_ipc_frame PRIVATE ${FUZZ_FLAGS})
target_link_options(fuzz_ipc_frame PRIVATE ${FUZZ_FLAGS})

# ── fuzz_bion_loader : .bion binary parser ──────────────────────────
add_executable(fuzz_bion_loader fuzz_bion_loader.cpp)
target_include_directories(fuzz_bion_loader PRIVATE "${CORE_INC}")
target_compile_options(fuzz_bion_loader PRIVATE ${FUZZ_FLAGS})
target_link_options(fuzz_bion_loader PRIVATE ${FUZZ_FLAGS})
add_library(bion_loader_fuzz_obj OBJECT "${CORE_SRC}/bion_loader_host.cpp")
target_include_directories(bion_loader_fuzz_obj PRIVATE "${CORE_INC}")
target_compile_options(bion_loader_fuzz_obj PRIVATE ${OBJ_FLAGS})
target_link_libraries(fuzz_bion_loader PRIVATE bion_loader_fuzz_obj)

# ── fuzz_swc_parser : SWC ASCII parser ──────────────────────────────
add_executable(fuzz_swc_parser fuzz_swc_parser.cpp)
target_include_directories(fuzz_swc_parser PRIVATE "${STUDIO_SRC}" "${CORE_INC}")
target_compile_options(fuzz_swc_parser PRIVATE ${FUZZ_FLAGS})
target_link_options(fuzz_swc_parser PRIVATE ${FUZZ_FLAGS})
add_library(swc_parser_fuzz_obj OBJECT "${STUDIO_SRC}/swc_parser.cpp")
target_include_directories(swc_parser_fuzz_obj PRIVATE "${STUDIO_SRC}" "${CORE_INC}")
target_compile_options(swc_parser_fuzz_obj PRIVATE ${OBJ_FLAGS})
target_link_libraries(fuzz_swc_parser PRIVATE swc_parser_fuzz_obj)

# ── fuzz_neuroml_xml : NeuroML XML parser ───────────────────────────
add_executable(fuzz_neuroml_xml fuzz_neuroml_xml.cpp)
target_include_directories(fuzz_neuroml_xml PRIVATE "${STUDIO_SRC}" "${CORE_INC}")
target_compile_options(fuzz_neuroml_xml PRIVATE ${FUZZ_FLAGS})
target_link_options(fuzz_neuroml_xml PRIVATE ${FUZZ_FLAGS})
add_library(neuroml_import_fuzz_obj OBJECT "${STUDIO_SRC}/neuroml_import.cpp")
target_include_directories(neuroml_import_fuzz_obj PRIVATE "${STUDIO_SRC}" "${CORE_INC}")
target_compile_options(neuroml_import_fuzz_obj PRIVATE ${OBJ_FLAGS})
add_library(i18n_fuzz_obj OBJECT "${STUDIO_SRC}/i18n.cpp")
target_include_directories(i18n_fuzz_obj PRIVATE "${STUDIO_SRC}" "${CORE_INC}")
target_compile_options(i18n_fuzz_obj PRIVATE ${OBJ_FLAGS})
target_link_libraries(fuzz_neuroml_xml PRIVATE neuroml_import_fuzz_obj i18n_fuzz_obj)

# ── Seed corpus ──────────────────────────────────────────────────────
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/corpus/mcp_json")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/corpus/plugin_desc")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/corpus/ipc_frame")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/corpus/bion_loader")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/corpus/swc_parser")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/corpus/neuroml_xml")

file(WRITE "${CMAKE_BINARY_DIR}/corpus/mcp_json/seed1.json"
    "{\"jsonrpc\":\"2.0\",\"method\":\"initialize\",\"params\":{},\"id\":1}\n")
file(WRITE "${CMAKE_BINARY_DIR}/corpus/plugin_desc/seed1.json"
    "{\"name\":\"Test\",\"version\":\"1.0\",\"author\":\"A\",\"category\":\"T\",\"modifiers\":[{\"id\":1}]}")
file(WRITE "${CMAKE_BINARY_DIR}/corpus/ipc_frame/seed1.bin"
    "\\x00\\x00\\x00\\x00\\x00\\x00\\x00\\x00")
file(WRITE "${CMAKE_BINARY_DIR}/corpus/swc_parser/seed1.swc"
    "# SWC neuron\\n1 1 0 0 0 5.0 -1\\n2 3 10 0 0 2.0 1\\n")
file(WRITE "${CMAKE_BINARY_DIR}/corpus/neuroml_xml/seed1.nml"
    "<?xml version=\\\"1.0\\\"?>\\n<neuroml></neuroml>\\n")

message(STATUS "Fuzz targets: 6 harnesses configured")
