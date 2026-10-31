# RexFabric: mod loading and runtime hooks for rexglue projects.
#
#   include(path/to/RexFabric/CMakeLists.txt)
#   rexglue_setup_target(<target>)
#   rexfabric_attach(<target>)
#
# The project's manifest needs a template_dir under [entrypoint], which RexFabric fills with the codegen
# templates its hook slots need:
#
#   [entrypoint]
#   template_dir = "generated/templates"
#
# The stock templates come from the rexglue-sdk source tree. RexFabric finds them through REXSDK_DIR, or set
# REXFABRIC_SDK_TEMPLATES to <rexglue-sdk>/resources/templates.

include_guard(GLOBAL)

include(${CMAKE_CURRENT_LIST_DIR}/cmake/rexfabric_templates.cmake)

set(REXFABRIC_SDK_TEMPLATES "" CACHE PATH "The rexglue-sdk's resources/templates folder, matching the SDK the project builds with")

# The manifest's template_dir, or a fatal error saying how to add it.
function(_rexfabric_template_dir out_var)
  file(GLOB manifests "${CMAKE_SOURCE_DIR}/*_manifest.toml")
  list(LENGTH manifests manifest_count)
  if(NOT manifest_count EQUAL 1)
    message(FATAL_ERROR "RexFabric: expected one *_manifest.toml in ${CMAKE_SOURCE_DIR}, found ${manifest_count}")
  endif()
  file(STRINGS "${manifests}" line REGEX "^[ \t]*template_dir[ \t]*=")
  if(NOT line MATCHES "\"([^\"]+)\"")
    message(FATAL_ERROR "RexFabric: ${manifests} has no template_dir. Add this under [entrypoint]:\n  template_dir = \"generated/templates\"")
  endif()
  set(${out_var} "${CMAKE_SOURCE_DIR}/${CMAKE_MATCH_1}" PARENT_SCOPE)
endfunction()

function(_rexfabric_sdk_templates out_var)
  if(REXFABRIC_SDK_TEMPLATES)
    set(dir "${REXFABRIC_SDK_TEMPLATES}")
  elseif(REXSDK_DIR)
    set(dir "${REXSDK_DIR}/resources/templates")
  endif()
  if(NOT dir OR NOT EXISTS "${dir}/codegen")
    message(FATAL_ERROR "RexFabric: can't find the SDK's codegen templates. Set REXFABRIC_SDK_TEMPLATES to <rexglue-sdk>/resources/templates.")
  endif()
  set(${out_var} "${dir}" PARENT_SCOPE)
endfunction()

# Runs once, after every module has added its fragments.
function(_rexfabric_finish)
  _rexfabric_sdk_templates(sdk_dir)
  _rexfabric_template_dir(out_dir)
  rexfabric_write_templates("${sdk_dir}" "${out_dir}")
endfunction()

function(rexfabric_attach target)
  get_property(attached GLOBAL PROPERTY REXFABRIC_ATTACHED)
  if(attached)
    message(FATAL_ERROR "RexFabric: already attached to ${attached}; a project attaches it once")
  endif()
  set_property(GLOBAL PROPERTY REXFABRIC_ATTACHED ${target})

  set(dir ${CMAKE_CURRENT_FUNCTION_LIST_DIR})
  target_sources(${target} PRIVATE ${dir}/src/api.cpp ${dir}/src/hooks.cpp ${dir}/src/library.cpp ${dir}/src/mods.cpp ${dir}/src/native_loader.cpp ${dir}/src/rexfabric.cpp)
  target_include_directories(${target} PRIVATE ${dir}/include)
  # The generated code includes rexfabric/hook_slot.h, and rexglue_setup_target compiles it in its own target.
  if(TARGET ${target}_recomp)
    target_include_directories(${target}_recomp PRIVATE ${dir}/include)
  endif()
  if(NOT WIN32)
    target_link_libraries(${target} PRIVATE ${CMAKE_DL_LIBS})
  endif()

  rexfabric_template_fragment(codegen/pch_h BEFORE "#ifndef DECLARE_REX_FUNC" FILE ${dir}/templates/pch_hooks.inja)
  rexfabric_template_fragment(codegen/init_cpp APPEND FILE ${dir}/templates/init_slots.inja)
  cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL _rexfabric_finish)
endfunction()
