cmake_minimum_required(VERSION 3.10)
project(TrayApp VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows specific settings
if(WIN32)
    add_executable(TrayApp WIN32
        src/main.cpp
        src/app.rc
    )
    
    target_compile_definitions(TrayApp PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        _WIN32_IE=0x0600
    )
    
    # Add include directories for resource files
    target_include_directories(TrayApp PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    
    # Link required Windows libraries
    target_link_libraries(TrayApp PRIVATE
        comctl32
        shell32
        user32
        gdi32
        kernel32
    )
    
    # Set output directory
    set_target_properties(TrayApp PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin/Debug"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin/Release"
    )
endif()

# Install rules
install(TARGETS TrayApp RUNTIME DESTINATION bin)