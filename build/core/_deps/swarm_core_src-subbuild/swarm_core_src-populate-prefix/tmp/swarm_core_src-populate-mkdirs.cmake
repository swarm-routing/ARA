# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/tmp/claude-0/-home-user/844ef203-bb92-5474-822f-3dad885c668a/scratchpad/pin43/ARA/build/core/_deps/swarm_core_src-src"
  "/tmp/claude-0/-home-user/844ef203-bb92-5474-822f-3dad885c668a/scratchpad/pin43/ARA/build/core/_deps/swarm_core_src-build"
  "/tmp/claude-0/-home-user/844ef203-bb92-5474-822f-3dad885c668a/scratchpad/pin43/ARA/build/core/_deps/swarm_core_src-subbuild/swarm_core_src-populate-prefix"
  "/tmp/claude-0/-home-user/844ef203-bb92-5474-822f-3dad885c668a/scratchpad/pin43/ARA/build/core/_deps/swarm_core_src-subbuild/swarm_core_src-populate-prefix/tmp"
  "/tmp/claude-0/-home-user/844ef203-bb92-5474-822f-3dad885c668a/scratchpad/pin43/ARA/build/core/_deps/swarm_core_src-subbuild/swarm_core_src-populate-prefix/src/swarm_core_src-populate-stamp"
  "/tmp/claude-0/-home-user/844ef203-bb92-5474-822f-3dad885c668a/scratchpad/pin43/ARA/build/core/_deps/swarm_core_src-subbuild/swarm_core_src-populate-prefix/src"
  "/tmp/claude-0/-home-user/844ef203-bb92-5474-822f-3dad885c668a/scratchpad/pin43/ARA/build/core/_deps/swarm_core_src-subbuild/swarm_core_src-populate-prefix/src/swarm_core_src-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/tmp/claude-0/-home-user/844ef203-bb92-5474-822f-3dad885c668a/scratchpad/pin43/ARA/build/core/_deps/swarm_core_src-subbuild/swarm_core_src-populate-prefix/src/swarm_core_src-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/tmp/claude-0/-home-user/844ef203-bb92-5474-822f-3dad885c668a/scratchpad/pin43/ARA/build/core/_deps/swarm_core_src-subbuild/swarm_core_src-populate-prefix/src/swarm_core_src-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
