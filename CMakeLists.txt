cmake_minimum_required(VERSION 3.15)
project(WindowsServiceApp VERSION 1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Fix for nlohmann_json compatibility with newer CMake
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.20")
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "")
endif()

# Find or download nlohmann_json
find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
    message(STATUS "nlohmann_json not found, will use FetchContent to download it")
    include(FetchContent)
    
    cmake_policy(PUSH)
    cmake_policy(SET CMP0167 OLD)
    
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.2
    )
    
    set(JSON_BuildTests OFF CACHE INTERNAL "")
    FetchContent_MakeAvailable(nlohmann_json)
    cmake_policy(POP)
endif()

if(WIN32)
    # Common compile options
    add_compile_options(/FS)
    
    # TrayService executable
    add_executable(TrayService WIN32
        src/service/service_main.cpp
        src/common/rpc_s.c
    )
    
    target_compile_definitions(TrayService PRIVATE
        UNICODE 
        _UNICODE 
        WIN32_LEAN_AND_MEAN 
        _CRT_SECURE_NO_WARNINGS
    )
    
    target_include_directories(TrayService PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/service
        ${CMAKE_CURRENT_SOURCE_DIR}/src/common
    )
    
    target_link_libraries(TrayService PRIVATE 
        nlohmann_json::nlohmann_json
        advapi32 
        user32 
        kernel32 
        wtsapi32 
        userenv 
        rpcrt4
        winhttp
        wininet
        crypt32
        bcrypt
    )
    
    # TrayApp executable
    add_executable(TrayApp WIN32
        src/app/main.cpp
        src/app/app.rc
        src/common/rpc_c.c
    )
    
    target_compile_definitions(TrayApp PRIVATE
        UNICODE 
        _UNICODE 
        WIN32_LEAN_AND_MEAN 
        _CRT_SECURE_NO_WARNINGS
    )
    
    target_include_directories(TrayApp PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/app
        ${CMAKE_CURRENT_SOURCE_DIR}/src/common
    )
    
    target_link_libraries(TrayApp PRIVATE 
        advapi32 
        user32 
        shell32 
        kernel32 
        comctl32 
        gdi32 
        rpcrt4
    )
    
    # Set output directory
    set_target_properties(TrayService TrayApp PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin/Debug"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin/Release"
    )
endif()