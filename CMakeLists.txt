cmake_minimum_required(VERSION 3.10)
project(AudSync)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include platform-specific configurations
if(WIN32)
    include(cmake/WindowsConfig.cmake OPTIONAL)
elseif(APPLE)
    include(cmake/macOSConfig.cmake OPTIONAL)
endif()

# Find required packages
find_package(Threads REQUIRED)

# Include directories
include_directories(include)

# Source files
set(COMMON_SOURCES
    src/AudioBuffer.cpp
    src/NetworkManager.cpp
)

set(CLIENT_SOURCES
    src/AudioClient.cpp
    src/AudioProcessor.cpp
    src/main_client.cpp
    ${COMMON_SOURCES}
)

set(SERVER_SOURCES
    src/AudioServer.cpp
    src/main_server.cpp
    ${COMMON_SOURCES}
)

# Platform-specific network libraries
if(WIN32)
    set(NETWORK_LIBRARIES ws2_32 wsock32)
else()
    set(NETWORK_LIBRARIES)
endif()

# Find PortAudio with cross-platform support
find_path(PORTAUDIO_INCLUDE_DIR portaudio.h
    PATHS 
        /usr/include 
        /usr/local/include
        ${CMAKE_PREFIX_PATH}/include
        $ENV{PORTAUDIO_ROOT}/include
        "C:/Program Files/PortAudio/include"
        "C:/Program Files (x86)/PortAudio/include"
)

find_library(PORTAUDIO_LIBRARY 
    NAMES portaudio portaudio_static portaudio_x64 portaudio_x86
    PATHS 
        /usr/lib 
        /usr/local/lib
        ${CMAKE_PREFIX_PATH}/lib
        $ENV{PORTAUDIO_ROOT}/lib
        "C:/Program Files/PortAudio/lib"
        "C:/Program Files (x86)/PortAudio/lib"
)

if(NOT PORTAUDIO_INCLUDE_DIR OR NOT PORTAUDIO_LIBRARY)
    message(FATAL_ERROR "PortAudio not found. Please install PortAudio development libraries.")
endif()

include_directories(${PORTAUDIO_INCLUDE_DIR})

# Create executables
add_executable(audsync_client ${CLIENT_SOURCES})
add_executable(audsync_server ${SERVER_SOURCES})

# Link libraries
target_link_libraries(audsync_client 
    ${PORTAUDIO_LIBRARY}
    ${NETWORK_LIBRARIES}
    Threads::Threads
)

target_link_libraries(audsync_server 
    ${NETWORK_LIBRARIES}
    Threads::Threads
)

# Add macOS frameworks if available
if(APPLE AND MACOS_AUDIO_FRAMEWORKS)
    target_link_libraries(audsync_client ${MACOS_AUDIO_FRAMEWORKS})
endif()

# Cross-platform compiler flags
if(MSVC)
    target_compile_options(audsync_client PRIVATE /W4)
    target_compile_options(audsync_server PRIVATE /W4)
    # Define WIN32_LEAN_AND_MEAN to reduce Windows header overhead
    target_compile_definitions(audsync_client PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_definitions(audsync_server PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
else()
    target_compile_options(audsync_client PRIVATE -Wall -Wextra)
    target_compile_options(audsync_server PRIVATE -Wall -Wextra)
endif()

# Windows-specific settings
if(WIN32)
    # Ensure we're linking against the correct Windows socket library
    target_link_libraries(audsync_client ws2_32)
    target_link_libraries(audsync_server ws2_32)
endif()
