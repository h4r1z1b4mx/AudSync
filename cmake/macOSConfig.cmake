# Additional CMake configuration for macOS builds
# This file can be included in CMakeLists.txt for macOS-specific settings

if(APPLE)
    # Set minimum macOS version
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum OS X deployment version")
    
    # Enable modern C++ features
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    
    # Framework search paths for PortAudio if installed as framework
    set(CMAKE_FRAMEWORK_PATH
        /Library/Frameworks
        /System/Library/Frameworks
        ${CMAKE_FRAMEWORK_PATH}
    )
    
    # Additional library search paths for Homebrew
    if(EXISTS /usr/local/lib)
        link_directories(/usr/local/lib)
    endif()
    if(EXISTS /opt/homebrew/lib)
        link_directories(/opt/homebrew/lib)
    endif()
    
    # Additional include paths for Homebrew
    if(EXISTS /usr/local/include)
        include_directories(/usr/local/include)
    endif()
    if(EXISTS /opt/homebrew/include)
        include_directories(/opt/homebrew/include)
    endif()
    
    # CoreAudio framework (sometimes needed with PortAudio on macOS)
    find_library(CORE_AUDIO_FRAMEWORK CoreAudio)
    find_library(AUDIO_UNIT_FRAMEWORK AudioUnit)
    find_library(AUDIO_TOOLBOX_FRAMEWORK AudioToolbox)
    
    if(CORE_AUDIO_FRAMEWORK AND AUDIO_UNIT_FRAMEWORK AND AUDIO_TOOLBOX_FRAMEWORK)
        set(MACOS_AUDIO_FRAMEWORKS 
            ${CORE_AUDIO_FRAMEWORK} 
            ${AUDIO_UNIT_FRAMEWORK} 
            ${AUDIO_TOOLBOX_FRAMEWORK}
        )
    endif()
endif()
