cmake_minimum_required(VERSION 3.20)
project(rbpo-app VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Convert logo.png → src/logo.ico at configure time (Windows + PowerShell)
# ---------------------------------------------------------------------------
set(ICON_PNG "${CMAKE_SOURCE_DIR}/logo.png")
set(ICON_ICO "${CMAKE_SOURCE_DIR}/src/logo.ico")

if(WIN32 AND EXISTS "${ICON_PNG}" AND NOT EXISTS "${ICON_ICO}")
    message(STATUS "Converting logo.png -> src/logo.ico ...")
    execute_process(
        COMMAND powershell -ExecutionPolicy Bypass
                -File "${CMAKE_SOURCE_DIR}/scripts/convert_icon.ps1"
                -InputFile "${ICON_PNG}"
                -OutputFile "${ICON_ICO}"
        RESULT_VARIABLE ICON_RESULT
    )
    if(NOT ICON_RESULT EQUAL 0)
        message(WARNING "Icon conversion failed (code ${ICON_RESULT}). "
                        "The app will use the default system icon.")
    endif()
endif()

# ---------------------------------------------------------------------------
# MIDL — generate RPC stubs from the IDL file
# ---------------------------------------------------------------------------
find_program(MIDL_COMPILER midl)
if(NOT MIDL_COMPILER)
    message(FATAL_ERROR "MIDL compiler not found. Install the Windows SDK.")
endif()

set(IDL_FILE "${CMAKE_SOURCE_DIR}/src/rpc/rbpo_rpc.idl")
set(ACF_FILE "${CMAKE_SOURCE_DIR}/src/rpc/rbpo_rpc.acf")
set(RPC_GEN_DIR "${CMAKE_BINARY_DIR}/rpc_gen")
file(MAKE_DIRECTORY "${RPC_GEN_DIR}")

set(RPC_HEADER "${RPC_GEN_DIR}/rbpo_rpc_h.h")
set(RPC_CLIENT "${RPC_GEN_DIR}/rbpo_rpc_c.c")
set(RPC_SERVER "${RPC_GEN_DIR}/rbpo_rpc_s.c")

add_custom_command(
    OUTPUT "${RPC_HEADER}" "${RPC_CLIENT}" "${RPC_SERVER}"
    COMMAND "${MIDL_COMPILER}"
        /acf "${ACF_FILE}"
        /header rbpo_rpc_h.h
        /cstub  rbpo_rpc_c.c
        /sstub  rbpo_rpc_s.c
        "${IDL_FILE}"
    DEPENDS "${IDL_FILE}" "${ACF_FILE}"
    WORKING_DIRECTORY "${RPC_GEN_DIR}"
    COMMENT "Running MIDL compiler on rbpo_rpc.idl"
)

add_custom_target(rpc_stubs DEPENDS "${RPC_HEADER}" "${RPC_CLIENT}" "${RPC_SERVER}")

# ---------------------------------------------------------------------------
# GUI Application target
# ---------------------------------------------------------------------------
add_executable(${PROJECT_NAME} WIN32
    src/main.cpp
    src/app.rc
    "${RPC_CLIENT}"
)

add_dependencies(${PROJECT_NAME} rpc_stubs)

target_include_directories(${PROJECT_NAME} PRIVATE src "${RPC_GEN_DIR}")

target_compile_definitions(${PROJECT_NAME} PRIVATE UNICODE _UNICODE)

# UTF-8 source and execution character set (for Cyrillic strings)
target_compile_options(${PROJECT_NAME} PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

target_link_libraries(${PROJECT_NAME} PRIVATE shell32 rpcrt4 advapi32)

# ---------------------------------------------------------------------------
# Windows Service target
# ---------------------------------------------------------------------------
add_executable(rbpo-service
    src/service/service_main.cpp
    "${RPC_SERVER}"
)

add_dependencies(rbpo-service rpc_stubs)

target_include_directories(rbpo-service PRIVATE src "${RPC_GEN_DIR}")

target_compile_definitions(rbpo-service PRIVATE UNICODE _UNICODE)

target_compile_options(rbpo-service PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

target_link_libraries(rbpo-service PRIVATE wtsapi32 userenv rpcrt4 advapi32)
