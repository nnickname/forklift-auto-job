cmake_minimum_required(VERSION 3.15)
project(samp-forklift-mod LANGUAGES CXX)

# GTA SA and SA-MP are 32-bit
set(CMAKE_GENERATOR_PLATFORM Win32)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Output as .asi (which is just a renamed .dll)
set(CMAKE_SHARED_LIBRARY_SUFFIX ".asi")

# Source files
set(SOURCES
    src/main.cpp
    src/game.cpp
)

set(HEADERS
    src/samp.h
    src/game.h
    src/coastguard.h
)

# Build as shared library (DLL -> ASI)
add_library(${PROJECT_NAME} SHARED ${SOURCES} ${HEADERS})

# No lib prefix on Windows
set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "")

# Ensure 32-bit compilation
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE 
        /W3          # Warning level 3
        /MT          # Static CRT (no runtime dependencies)
        /O2          # Optimize for speed
        /GS-         # Disable buffer security checks (smaller binary)
    )
    target_link_options(${PROJECT_NAME} PRIVATE
        /SUBSYSTEM:WINDOWS
        /MACHINE:X86
    )
endif()

# Link against Windows libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    kernel32
    user32
)

# Install: copy .asi to GTA SA directory (set your path)
# set(GTA_SA_DIR "C:/Games/GTA San Andreas")
# install(TARGETS ${PROJECT_NAME} DESTINATION ${GTA_SA_DIR})

message(STATUS "==============================================")
message(STATUS " SA-MP Forklift Auto-Job Mod (Anticheat Test)")
message(STATUS " Output: ${PROJECT_NAME}.asi")
message(STATUS " Platform: Win32 (x86)")
message(STATUS "==============================================")
