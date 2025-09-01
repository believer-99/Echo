cmake_minimum_required(VERSION 3.10)

# Project name and version
project(echo VERSION 0.1 LANGUAGES CXX)

# Use C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Paths
# Headers live under src/include and sources under src/*.cpp
include_directories(${PROJECT_SOURCE_DIR}/src)
include_directories(${PROJECT_SOURCE_DIR}/src/include)
file(GLOB SOURCES ${PROJECT_SOURCE_DIR}/src/*.cpp)

# Executable
add_executable(echo ${SOURCES})

# Link pthread (for multithreading)
find_package(Threads REQUIRED)
target_link_libraries(echo PRIVATE Threads::Threads)

# Link OpenSSL Crypto for SHA256
find_package(OpenSSL REQUIRED)
target_link_libraries(echo PRIVATE OpenSSL::Crypto)

# Optional: Enable ncurses TUI for notepad
option(ECHO_ENABLE_UI "Build with Qt-based UI" OFF)
if (ECHO_ENABLE_UI)
	target_compile_definitions(echo PRIVATE ECHO_ENABLE_UI)
	set(CMAKE_AUTOMOC ON)
	find_package(Qt5 COMPONENTS Widgets REQUIRED)
	target_link_libraries(echo PRIVATE Qt5::Widgets)
endif()
