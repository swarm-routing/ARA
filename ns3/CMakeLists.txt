# Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
# SPDX-License-Identifier: GPL-2.0-only
#
# The `ara` ns-3 contrib module (#5): ARA's simulator-free core/ (one
# directory up, MIT OR GPL-2.0-only) compiled into ns-3 on swarm-core, with the
# adapter that carries out its decisions (model/), a helper, the harness arm
# (helper/ara-bench-arms.cc, compiled only when routing-bench is installed
# beside it), an example and the test suite. Install it next to swarm-core:
#
#   ns3-build-contrib --test swarm-core=/src/ns3-swarm-core ara=/src/ara/ns3
#
# ns-3 reaches this directory through contrib/ara, a link to the checkout's
# ns3/: core/ is found beside the link's target, not beside the link.
get_filename_component(ara_ns3_dir "${CMAKE_CURRENT_SOURCE_DIR}" REALPATH)  # CMake < 3.19 too
get_filename_component(ara_core_dir "${ara_ns3_dir}/../core" ABSOLUTE)
# core/ includes swarm-core's primitives without the ns3/ prefix (they are the
# same files as ns3/swarm-*.h), so it builds alone as well.
set(ara_swarm_core_dir "${PROJECT_SOURCE_DIR}/contrib/swarm-core/core")
foreach(d "${ara_core_dir}" "${ara_swarm_core_dir}")
  if(NOT EXISTS "${d}")
    message(FATAL_ERROR "ara: ${d} not found (install swarm-core as contrib/swarm-core)")
  endif()
endforeach()
include_directories("${ara_core_dir}" "${ara_swarm_core_dir}")

# The harness arm, only where swarm-routing/ns3-tools' routing-bench is
# installed and enabled. Its library is named through a variable: ns-3 scans
# module CMakeLists for library tokens, and `ara` must also build alone
# (routing-bench README, "Registering an arm").
set(ara_bench_sources)
set(ara_bench_libs)
set(ara_bench_module routing-bench)
if(EXISTS ${PROJECT_SOURCE_DIR}/contrib/${ara_bench_module}/CMakeLists.txt
   AND ara_bench_module IN_LIST ns3-all-enabled-modules)
  set(ara_bench_sources helper/ara-bench-arms.cc)
  set(ara_bench_lib lib${ara_bench_module})
  set(ara_bench_libs ${${ara_bench_lib}})
endif()

build_lib(
  LIBNAME ara
  SOURCE_FILES
    ${ara_core_dir}/ara-ant.cc
    ${ara_core_dir}/ara-router.cc
    model/ara-header.cc
    model/ara-routing-protocol.cc
    helper/ara-helper.cc
    ${ara_bench_sources}
  HEADER_FILES
    model/ara-routing-protocol.h
    helper/ara-helper.h
  LIBRARIES_TO_LINK ${libswarm-core} ${libcore} ${libnetwork} ${libinternet} ${ara_bench_libs}
  TEST_SOURCES
    ${ara_core_dir}/test/all-tests.cc
    ${ara_core_dir}/test/test-ant-codec.cc
    ${ara_core_dir}/test/test-network.cc
    ${ara_core_dir}/test/test-router.cc
    test/ara-test-suite.cc
)
