# Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
# SPDX-License-Identifier: MIT OR GPL-2.0-only
#
# ARA's simulator-free core on its own: a static library and its tests, with
# no ns-3 tree. It builds on swarm-core's dual-licensed primitives
# (swarm-routing/ns3-swarm-core core/): the ports, RouteDecision, the
# pheromone table, the dedup history, evaporation and the wire helpers. The
# ns-3 module (ns3/, to come) compiles the same sources against the swarm-core
# contrib module and runs the same tests under test.py.
#
#   cmake -S core -B build/core && cmake --build build/core && ctest --test-dir build/core
#
# SWARM_CORE_DIR: a local swarm-core core/ directory; empty fetches
# SWARM_CORE_REF (a full commit SHA, bumped by PR) from GitHub.
# ARA_CORE_SANITIZE=ON adds ASan + UBSan, as the sanitized CI leg.
cmake_minimum_required(VERSION 3.20)
project(ara_core LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(SWARM_CORE_DIR "" CACHE PATH "swarm-core's core/ directory; empty: fetch SWARM_CORE_REF")
set(SWARM_CORE_REF "e20aad3f92dfd253d7c6411f648b90a32eee5038" CACHE STRING
    "swarm-routing/ns3-swarm-core commit to fetch when SWARM_CORE_DIR is empty")
option(ARA_CORE_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

# Sanitizer flags first: they are directory options, so swarm-core's sources,
# added below, are instrumented too.
if(ARA_CORE_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

# swarm-core's core/ as a subproject: it defines the library target
# swarm::core (its own source list, so a new core/*.cc needs no edit here) and
# nothing else: no test target, no ctest entry, no -Werror on our sources
# (swarm-routing/ns3-swarm-core#100; its README "Use core/ from another CMake
# project").
if(SWARM_CORE_DIR)
  if(NOT EXISTS "${SWARM_CORE_DIR}/swarm-ports.h")
    message(FATAL_ERROR "SWARM_CORE_DIR=${SWARM_CORE_DIR} is not swarm-core's core/ directory")
  endif()
  add_subdirectory(${SWARM_CORE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/swarm-core)
else()
  include(FetchContent)
  FetchContent_Declare(swarm_core_src
    GIT_REPOSITORY https://github.com/swarm-routing/ns3-swarm-core.git
    GIT_TAG ${SWARM_CORE_REF}
    SOURCE_SUBDIR core)
  FetchContent_MakeAvailable(swarm_core_src)
endif()

# The strict warnings the org CI applies (ns3-containers' swarm-quality.cmake),
# on ARA's own sources only.
# A swarm-core deprecation ([[deprecated]], its docs/api-policy.md
# "Deprecation") stays a warning: it shows in every build, never fails one.
set(ARA_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wold-style-cast
                 -Wcast-align -Woverloaded-virtual -Wnull-dereference -Wformat=2 -Werror
                 -Wno-error=deprecated-declarations)

add_library(ara_core STATIC
  ara-ant.cc
  ara-router.cc
)
target_include_directories(ara_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ara_core PUBLIC swarm::core)
target_compile_options(ara_core PRIVATE ${ARA_WARNINGS})

add_executable(ara_core_tests
  test/all-tests.cc
  test/run-core-tests.cc
  test/test-ant-codec.cc
  test/test-failure.cc
  test/test-maintenance.cc
  test/test-network.cc
  test/test-observer.cc
  test/test-router.cc
)
target_link_libraries(ara_core_tests PRIVATE ara_core)
target_compile_options(ara_core_tests PRIVATE ${ARA_WARNINGS})

enable_testing()
add_test(NAME ara-core COMMAND ara_core_tests)
