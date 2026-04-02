cmake_minimum_required(VERSION 3.20)
project(rbpo-app VERSION 1.0 LANGUAGES CXX)

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
# Application target
# ---------------------------------------------------------------------------
add_executable(${PROJECT_NAME} WIN32
    src/main.cpp
    src/app.rc
)

target_include_directories(${PROJECT_NAME} PRIVATE src)

target_compile_definitions(${PROJECT_NAME} PRIVATE UNICODE _UNICODE)

# UTF-8 source and execution character set (for Cyrillic strings)
target_compile_options(${PROJECT_NAME} PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

target_link_libraries(${PROJECT_NAME} PRIVATE shell32)
