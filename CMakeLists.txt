cmake_minimum_required(VERSION 3.10)
project(AudSync)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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

# Find PortAudio
find_path(PORTAUDIO_INCLUDE_DIR portaudio.h
    PATHS /usr/include /usr/local/include
)

find_library(PORTAUDIO_LIBRARY NAMES portaudio
    PATHS /usr/lib /usr/local/lib
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
    Threads::Threads
)

target_link_libraries(audsync_server 
    Threads::Threads
)

# Compiler flags
target_compile_options(audsync_client PRIVATE -Wall -Wextra)
target_compile_options(audsync_server PRIVATE -Wall -Wextra)
