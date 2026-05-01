cmake_minimum_required(VERSION 3.15)
project(WindowsServiceApp VERSION 1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(WIN32)
    # Используем предварительно сгенерированные файлы
    set(RPC_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/common)
    
    if(NOT EXISTS ${RPC_SOURCE_DIR}/service_rpc.h OR 
       NOT EXISTS ${RPC_SOURCE_DIR}/rpc_c.c OR 
       NOT EXISTS ${RPC_SOURCE_DIR}/rpc_s.c)
        message(FATAL_ERROR "RPC files not found in ${RPC_SOURCE_DIR}! Run generate_rpc.bat first.")
    endif()
    
    message(STATUS "Using RPC files from: ${RPC_SOURCE_DIR}")
    
    # Сервис
    add_executable(TrayService WIN32
        src/service/service_main.cpp
        ${RPC_SOURCE_DIR}/rpc_s.c
    )
    
    target_compile_definitions(TrayService PRIVATE
        UNICODE 
        _UNICODE 
        WIN32_LEAN_AND_MEAN 
        _CRT_SECURE_NO_WARNINGS
    )
    
    target_include_directories(TrayService PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/service
        ${RPC_SOURCE_DIR}
    )
    
    target_link_libraries(TrayService PRIVATE 
        advapi32 
        user32 
        kernel32 
        wtsapi32 
        userenv 
        rpcrt4 
        winhttp
    )
    
    # Приложение
    add_executable(TrayApp WIN32
        src/app/main.cpp
        src/app/app.rc
        ${RPC_SOURCE_DIR}/rpc_c.c
    )
    
    target_compile_definitions(TrayApp PRIVATE
        UNICODE 
        _UNICODE 
        WIN32_LEAN_AND_MEAN 
        _CRT_SECURE_NO_WARNINGS
    )
    
    target_include_directories(TrayApp PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/app
        ${RPC_SOURCE_DIR}
    )
    
    target_link_libraries(TrayApp PRIVATE 
        advapi32 
        user32 
        shell32 
        kernel32 
        comctl32 
        gdi32 
        rpcrt4 
        winhttp
    )
    
    set_target_properties(TrayService TrayApp PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
    
    message(STATUS "TrayService sources: ${RPC_SOURCE_DIR}/rpc_s.c")
    message(STATUS "TrayApp sources: ${RPC_SOURCE_DIR}/rpc_c.c")
endif()