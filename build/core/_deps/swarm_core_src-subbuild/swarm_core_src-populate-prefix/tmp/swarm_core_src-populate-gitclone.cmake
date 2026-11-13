# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

if(EXISTS "/tmp/claude-0/-home-user/844ef203-bb92-5474-822f-3dad885c668a/scratchpad/pin43/ARA/build/core/_deps/swarm_core_src-subbuild/swarm_core_src-populate-prefix/src/swarm_core_src-populate-stamp/swarm_core_src-populate-gitclone-lastrun.txt" AND EXISTS "/tmp/claude-0/-home-user/844ef203-bb92-5474-822f-3dad885c668a/scratchpad/pin43/ARA/build/core/_deps/swarm_core_src-subbuild/swarm_core_src-populate-prefix/src/swarm_core_src-populate-stamp/swarm_core_src-populate-gitinfo.txt" AND
  "/tmp/claude-0/-home-user/844ef203-bb92-5474-822f-3dad885c668a/scratchpad/pin43/ARA/build/core/_deps/swarm_core_src-subbuild/swarm_core_src-populate-prefix/src/swarm_core_src-populate-stamp/swarm_core_src-populate-gitclone-lastrun.txt" IS_NEWER_THAN "/tmp/claude-0/-home-user/844ef203-bb92-5474-822f-3dad885c668a/scratchpad/pin43/ARA/build/core/_deps/swarm_core_src-subbuild/swarm_core_src-populate-prefix/src/swarm_core_src-populate-stamp/swarm_core_src-populate-gitinfo.txt")
  message(STATUS
    "Avoiding repeated git clone, stamp file is up to date: "
    "'/tmp/claude-0/-home-user/844ef203-bb92-5474-822f-3dad885c668a/scratchpad/pin43/ARA/build/core/_deps/swarm_core_src-subbuild/swarm_core_src-populate-prefix/src/swarm_core_src-populate-stamp/swarm_core_src-populate-gitclone-lastrun.txt'"
  )
  return()
endif()

execute_process(
  COMMAND ${CMAKE_COMMAND} -E rm -rf "/tmp/claude-0/-home-user/844ef203-bb92-5474-822f-3dad885c668a/scratchpad/pin43/ARA/build/core/_deps/swarm_core_src-src"
  RESULT_VARIABLE error_code
)
if(error_code)
  message(FATAL_ERROR "Failed to remove directory: '/tmp/claude-0/-home-user/844ef203-bb92-5474-822f-3dad885c668a/scratchpad/pin43/ARA/build/core/_deps/swarm_core_src-src'")
endif()

# try the clone 3 times in case there is an odd git clone issue
set(error_code 1)
set(number_of_tries 0)
while(error_code AND number_of_tries LESS 3)
  execute_process(
    COMMAND "/usr/bin/git"
            clone --no-checkout --config "advice.detachedHead=false" "https://github.com/swarm-routing/ns3-swarm-core.git" "swarm_core_src-src"
    WORKING_DIRECTORY "/tmp/claude-0/-home-user/844ef203-bb92-5474-822f-3dad885c668a/scratchpad/pin43/ARA/build/core/_deps"
    RESULT_VARIABLE error_code
  )
  math(EXPR number_of_tries "${number_of_tries} + 1")
endwhile()
if(number_of_tries GREATER 1)
  message(STATUS "Had to git clone more than once: ${number_of_tries} times.")
endif()
if(error_code)
  message(FATAL_ERROR "Failed to clone repository: 'https://github.com/swarm-routing/ns3-swarm-core.git'")
endif()

execute_process(
  COMMAND "/usr/bin/git"
          checkout "501dd7eca8c530dadaeb5d43c66e9c31d19fd8f5" --
  WORKING_DIRECTORY "/tmp/claude-0/-home-user/844ef203-bb92-5474-822f-3dad885c668a/scratchpad/pin43/ARA/build/core/_deps/swarm_core_src-src"
  RESULT_VARIABLE error_code
)
if(error_code)
  message(FATAL_ERROR "Failed to checkout tag: '501dd7eca8c530dadaeb5d43c66e9c31d19fd8f5'")
endif()

set(init_submodules TRUE)
if(init_submodules)
  execute_process(
    COMMAND "/usr/bin/git" 
            submodule update --recursive --init 
    WORKING_DIRECTORY "/tmp/claude-0/-home-user/844ef203-bb92-5474-822f-3dad885c668a/scratchpad/pin43/ARA/build/core/_deps/swarm_core_src-src"
    RESULT_VARIABLE error_code
  )
endif()
if(error_code)
  message(FATAL_ERROR "Failed to update submodules in: '/tmp/claude-0/-home-user/844ef203-bb92-5474-822f-3dad885c668a/scratchpad/pin43/ARA/build/core/_deps/swarm_core_src-src'")
endif()

# Complete success, update the script-last-run stamp file:
#
execute_process(
  COMMAND ${CMAKE_COMMAND} -E copy "/tmp/claude-0/-home-user/844ef203-bb92-5474-822f-3dad885c668a/scratchpad/pin43/ARA/build/core/_deps/swarm_core_src-subbuild/swarm_core_src-populate-prefix/src/swarm_core_src-populate-stamp/swarm_core_src-populate-gitinfo.txt" "/tmp/claude-0/-home-user/844ef203-bb92-5474-822f-3dad885c668a/scratchpad/pin43/ARA/build/core/_deps/swarm_core_src-subbuild/swarm_core_src-populate-prefix/src/swarm_core_src-populate-stamp/swarm_core_src-populate-gitclone-lastrun.txt"
  RESULT_VARIABLE error_code
)
if(error_code)
  message(FATAL_ERROR "Failed to copy script-last-run stamp file: '/tmp/claude-0/-home-user/844ef203-bb92-5474-822f-3dad885c668a/scratchpad/pin43/ARA/build/core/_deps/swarm_core_src-subbuild/swarm_core_src-populate-prefix/src/swarm_core_src-populate-stamp/swarm_core_src-populate-gitclone-lastrun.txt'")
endif()
