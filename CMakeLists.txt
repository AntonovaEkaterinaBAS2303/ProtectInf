cmake_minimum_required(VERSION 3.15)
project(WindowsServiceApp VERSION 1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(WIN32)
    add_executable(TrayService WIN32
    src/service/service_main.cpp
    src/common/rpc_s.c
    )
    
    target_compile_definitions(TrayService PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS
    )
    
    target_include_directories(TrayService PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/service
        ${CMAKE_CURRENT_SOURCE_DIR}/src/common
    )
    
    target_link_libraries(TrayService PRIVATE 
        advapi32 user32 kernel32 wtsapi32 userenv rpcrt4
    )
    
    add_executable(TrayApp WIN32
    src/app/main.cpp
    src/app/app.rc
    src/common/rpc_c.c
    )
    
    target_compile_definitions(TrayApp PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS
    )
    
    target_include_directories(TrayApp PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/app
        ${CMAKE_CURRENT_SOURCE_DIR}/src/common
    )
    
    target_link_libraries(TrayApp PRIVATE 
        advapi32 user32 shell32 kernel32 comctl32 gdi32 rpcrt4
    )
    
    set_target_properties(TrayService TrayApp PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
endif()