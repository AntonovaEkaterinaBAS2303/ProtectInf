cmake_minimum_required(VERSION 3.15)
project(WindowsServiceApp VERSION 1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(WIN32)
    find_program(MIDL_COMPILER midl)
    
    if(MIDL_COMPILER)
        set(RPC_IDL_FILE ${CMAKE_CURRENT_SOURCE_DIR}/src/common/rpc_interface.idl)
        set(RPC_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
        
        # Generate RPC stubs
        add_custom_command(
            OUTPUT ${RPC_OUTPUT_DIR}/service_rpc.h
                   ${RPC_OUTPUT_DIR}/rpc_c.c
                   ${RPC_OUTPUT_DIR}/rpc_s.c
            COMMAND ${MIDL_COMPILER} 
                    /h ${RPC_OUTPUT_DIR}/service_rpc.h
                    /cstub ${RPC_OUTPUT_DIR}/rpc_c.c
                    /sstub ${RPC_OUTPUT_DIR}/rpc_s.c
                    /out ${RPC_OUTPUT_DIR}
                    ${RPC_IDL_FILE}
            DEPENDS ${RPC_IDL_FILE}
            COMMENT "Generating RPC stubs from IDL..."
        )
        
        # Custom target for RPC generation
        add_custom_target(GenerateRpcStubs DEPENDS
            ${RPC_OUTPUT_DIR}/service_rpc.h
            ${RPC_OUTPUT_DIR}/rpc_c.c
            ${RPC_OUTPUT_DIR}/rpc_s.c
        )
        
        # TrayService depends on generated stubs
        add_executable(TrayService WIN32
            src/service/service_main.cpp
            ${RPC_OUTPUT_DIR}/rpc_s.c
        )
        add_dependencies(TrayService GenerateRpcStubs)
        
        target_compile_definitions(TrayService PRIVATE
            UNICODE _UNICODE WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS
        )
        
        target_include_directories(TrayService PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src/service
            ${RPC_OUTPUT_DIR}
        )
        
        target_link_libraries(TrayService PRIVATE 
            advapi32 user32 kernel32 wtsapi32 userenv rpcrt4 winhttp
        )
        
        # TrayApp depends on generated stubs
        add_executable(TrayApp WIN32
            src/app/main.cpp
            src/app/app.rc
            ${RPC_OUTPUT_DIR}/rpc_c.c
        )
        add_dependencies(TrayApp GenerateRpcStubs)
        
        target_compile_definitions(TrayApp PRIVATE
            UNICODE _UNICODE WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS
        )
        
        target_include_directories(TrayApp PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src/app
            ${RPC_OUTPUT_DIR}
        )
        
        target_link_libraries(TrayApp PRIVATE 
            advapi32 user32 shell32 kernel32 comctl32 gdi32 rpcrt4 winhttp
        )
        
        set_target_properties(TrayService TrayApp PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        )
    else()
        message(FATAL_ERROR "MIDL compiler not found! Please install Windows SDK.")
    endif()
endif()