cmake_minimum_required(VERSION 3.10)
project(CPPAudioMixer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(ALSA REQUIRED alsa)

add_executable(CPPAudioMixer CPPAudioMixer.cpp)

target_include_directories(CPPAudioMixer PRIVATE ${ALSA_INCLUDE_DIRS})
target_link_libraries(CPPAudioMixer PRIVATE ${ALSA_LIBRARIES})
