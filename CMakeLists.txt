cmake_minimum_required(VERSION 3.15)
project(WindowsServiceApp VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(OpenSSL QUIET)

# Windows specific settings
if(WIN32)
    # Common include directories
    set(COMMON_INCLUDES
        ${CMAKE_CURRENT_SOURCE_DIR}/src/common
    )
    
    # Common libraries
    set(COMMON_LIBS
        rpcrt4
        advapi32
        user32
        shell32
        kernel32
        userenv
        wtsapi32
    )
    
    # ========== Windows Service ==========
    add_executable(TrayService WIN32
        src/service/service_main.cpp
        src/service/service_controller.cpp
    )
    
    target_compile_definitions(TrayService PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
    )
    
    target_include_directories(TrayService PRIVATE
        ${COMMON_INCLUDES}
        ${CMAKE_CURRENT_SOURCE_DIR}/src/service
    )
    
    target_link_libraries(TrayService PRIVATE
        ${COMMON_LIBS}
    )
    
    # ========== GUI Application ==========
    add_executable(TrayApp WIN32
        src/app/main.cpp
        src/app/app.rc
    )
    
    target_compile_definitions(TrayApp PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
    )
    
    target_include_directories(TrayApp PRIVATE
        ${COMMON_INCLUDES}
        ${CMAKE_CURRENT_SOURCE_DIR}/src/app
    )
    
    target_link_libraries(TrayApp PRIVATE
        ${COMMON_LIBS}
        comctl32
        gdi32
    )
    
    # ========== Install Rules ==========
    install(TARGETS TrayService RUNTIME DESTINATION bin)
    install(TARGETS TrayApp RUNTIME DESTINATION bin)
endif()

# Copy DLLs and dependencies to output directory
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/bin)