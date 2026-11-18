cmake_minimum_required(VERSION 3.22)

# Settle - Seat Collision for SMP Cloth - Robert's SKSE plugin.
# Address-library based (SE/AE); tested on 1.6.1170.
project(Settle VERSION 1.1.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

if("${PROJECT_SOURCE_DIR}" STREQUAL "${PROJECT_BINARY_DIR}")
	message(FATAL_ERROR "in-source builds are not allowed")
endif()

# CommonLibSSE-NG (alandtse/CommonLibVR, branch ng - the same library Faster HDT-SMP builds against).
# Flat SE/AE only: VR off so the openvr submodule isn't needed.
set(ENABLE_SKYRIM_SE ON CACHE BOOL " " FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL " " FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL " " FORCE)
set(BUILD_TESTS OFF CACHE BOOL "Disable CommonLib tests" FORCE)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/extern/CommonLibSSE CommonLibSSE EXCLUDE_FROM_ALL)
get_target_property(COMMONLIB_SRC_DIR CommonLibSSE SOURCE_DIR)
include(${COMMONLIB_SRC_DIR}/cmake/CommonLibSSE.cmake)

add_commonlibsse_plugin(${PROJECT_NAME}
	NAME "Settle"
	AUTHOR "Robert"
	USE_ADDRESS_LIBRARY
	SOURCES src/main.cpp
)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
target_compile_definitions(${PROJECT_NAME} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_precompile_headers(${PROJECT_NAME} PRIVATE include/PCH.h)
