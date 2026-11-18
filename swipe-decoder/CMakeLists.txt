# futo-swipe-decoder: FUTO Swipe's neural swipe decoder as a helper process
# for futo-osk. Built against FUTO's swipe-library and ExecuTorch, so it is
# GPL-3.0 and runs as a separate program from futo-osk (see README.md).
cmake_minimum_required(VERSION 3.17)
project(futo_swipe_decoder LANGUAGES CXX)

include(GNUInstallDirs)

set(SWIPE_LIBRARY_DIR "" CACHE PATH "Path to a swipe-library checkout (gitlab.futo.org/keyboard/swipe-library)")
if(NOT SWIPE_LIBRARY_DIR)
    message(FATAL_ERROR "Set -DSWIPE_LIBRARY_DIR")
endif()
# Packages must run on any CPU of the architecture.
set(USE_MARCH_NATIVE OFF CACHE BOOL "" FORCE)
add_subdirectory(${SWIPE_LIBRARY_DIR} swipe-library EXCLUDE_FROM_ALL)
# swipe-library defines an "executorch" target carrying ExecuTorch's
# include paths and (whole-archive) libraries when it finds a build.
if(NOT TARGET executorch)
    message(FATAL_ERROR "ExecuTorch not found; set -DEXECUTORCH_ROOT and -DET_BUILD")
endif()

add_executable(futo-swipe-decoder futo-swipe-decoder.cpp)
target_link_libraries(futo-swipe-decoder PRIVATE swipe_decoder executorch pthread dl)
install(TARGETS futo-swipe-decoder DESTINATION ${CMAKE_INSTALL_LIBEXECDIR})
