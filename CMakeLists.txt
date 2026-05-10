cmake_minimum_required(VERSION 3.15)
project(WindowsServiceApp VERSION 1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find or download nlohmann_json
find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
    message(STATUS "nlohmann_json not found, will use FetchContent to download it")
    include(FetchContent)
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.2
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

if(WIN32)
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
    )
    
    # Add WINHTTP library for HTTPS requests
    target_link_libraries(TrayService PRIVATE winhttp)
    
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
    
    # Set output directory for both targets
    set_target_properties(TrayService TrayApp PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin/Debug"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin/Release"
    )
    
    # Set additional properties for TrayService
    set_target_properties(TrayService PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
    
    # Set additional properties for TrayApp
    set_target_properties(TrayApp PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
endif()