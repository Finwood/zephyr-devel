# SPDX-License-Identifier: Apache-2.0
#
# Link compile_commands.json from the build directory to the workspace root
# after manual west builds. Skipped for Twister (build dir under twister-out/).

get_filename_component(WORKSPACE_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(WORKSPACE_ROOT "${WORKSPACE_ROOT}" CACHE INTERNAL "Workspace root")

function(workspace_export_compile_commands)
  if(DEFINED TC_RUNID OR DEFINED TC_NAME)
    # skip if running in Twister
    return()
  endif()

  if(NOT TARGET zephyr_final)
    return()
  endif()

  set(_src "${CMAKE_BINARY_DIR}/compile_commands.json")
  set(_dest "${WORKSPACE_ROOT}/compile_commands.json")

  add_custom_target(export_compile_commands ALL
    COMMAND ${CMAKE_COMMAND} -E rm -f "${_dest}"
    COMMAND ${CMAKE_COMMAND} -E create_symlink "${_src}" "${_dest}"
    COMMENT "Linking compile_commands.json to workspace root"
    VERBATIM
  )
  add_dependencies(export_compile_commands zephyr_final)
endfunction()

cmake_language(DEFER DIRECTORY "${APPLICATION_SOURCE_DIR}" CALL workspace_export_compile_commands)
