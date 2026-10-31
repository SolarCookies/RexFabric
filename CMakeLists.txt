# RexFabric: mods for rexglue projects. Runtime hooks on every recompiled function, native and Lua mods, the
# game's own modding API, and a mods menu with a Thunderstore browser.
#
#   include(path/to/RexFabric/CMakeLists.txt)
#   rexglue_setup_target(<target>)
#   rexfabric_attach(<target> [WITH_GUIDE])
#
# WITH_GUIDE puts the mods menu in RexLiveless's Guide; attach RexLiveless to the target first. Without it the
# menu is plain ImGui windows.
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

# miniz is C. This file is included, so this runs at the including project's scope, as enable_language must.
enable_language(C)

include(${CMAKE_CURRENT_LIST_DIR}/cmake/rexfabric_templates.cmake)

set(REXFABRIC_SDK_TEMPLATES "" CACHE PATH "The rexglue-sdk's resources/templates folder, matching the SDK the project builds with")

# Lua 5.4.8, compiled as C++ so a Lua error unwinds C++ frames instead of jumping over them.
file(GLOB _rexfabric_lua_sources ${CMAKE_CURRENT_LIST_DIR}/thirdparty/lua/src/*.c)
set_source_files_properties(${_rexfabric_lua_sources} PROPERTIES LANGUAGE CXX)
add_library(rexfabric_lua STATIC ${_rexfabric_lua_sources})
target_include_directories(rexfabric_lua PUBLIC ${CMAKE_CURRENT_LIST_DIR}/thirdparty/lua/src)
target_compile_options(rexfabric_lua PRIVATE -w)
if(UNIX AND NOT APPLE)
  target_compile_definitions(rexfabric_lua PRIVATE LUA_USE_LINUX)
elseif(APPLE)
  target_compile_definitions(rexfabric_lua PRIVATE LUA_USE_MACOSX)
endif()

# miniz 3.0.2, for Thunderstore's zip archives.
add_library(rexfabric_miniz STATIC ${CMAKE_CURRENT_LIST_DIR}/thirdparty/miniz/miniz.c)
target_include_directories(rexfabric_miniz PUBLIC ${CMAKE_CURRENT_LIST_DIR}/thirdparty/miniz)
target_compile_definitions(rexfabric_miniz PUBLIC MINIZ_NO_ZLIB_COMPATIBLE_NAMES=1)
target_compile_options(rexfabric_miniz PRIVATE -w)

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
  cmake_parse_arguments(PARSE_ARGV 1 arg "WITH_GUIDE" "" "")
  get_property(attached GLOBAL PROPERTY REXFABRIC_ATTACHED)
  if(attached)
    message(FATAL_ERROR "RexFabric: already attached to ${attached}; a project attaches it once")
  endif()
  set_property(GLOBAL PROPERTY REXFABRIC_ATTACHED ${target})

  set(dir ${CMAKE_CURRENT_FUNCTION_LIST_DIR})
  set(sources
    src/api.cpp src/events.cpp src/game_api.cpp src/hooks.cpp src/host_input.cpp src/library.cpp src/midasm.cpp src/mods.cpp src/native_loader.cpp src/platform.cpp src/rexfabric.cpp src/switches.cpp
    src/lua/lua_game.cpp src/lua/lua_host.cpp src/lua/lua_rex.cpp src/lua/lua_vm.cpp
    src/net/http.cpp src/net/thunderstore.cpp
    src/ui/imgui_menu.cpp src/ui/menu_model.cpp
  )
  if(arg_WITH_GUIDE)
    list(APPEND sources src/ui/guide_menu.cpp)
    target_compile_definitions(${target} PRIVATE REXFABRIC_WITH_GUIDE=1)
  endif()
  list(TRANSFORM sources PREPEND ${dir}/)
  target_sources(${target} PRIVATE ${sources})
  target_include_directories(${target} PRIVATE ${dir}/include ${dir}/thirdparty)
  target_link_libraries(${target} PRIVATE rexfabric_lua rexfabric_miniz)
  # The generated code includes rexfabric/hook_slot.h, and rexglue_setup_target compiles it in its own target.
  if(TARGET ${target}_recomp)
    target_include_directories(${target}_recomp PRIVATE ${dir}/include)
  endif()
  if(WIN32)
    target_link_libraries(${target} PRIVATE winhttp shell32)
  else()
    target_link_libraries(${target} PRIVATE ${CMAKE_DL_LIBS})
  endif()

  rexfabric_template_fragment(codegen/pch_h BEFORE "#ifndef DECLARE_REX_FUNC" FILE ${dir}/templates/pch_hooks.inja)
  rexfabric_template_fragment(codegen/init_cpp APPEND FILE ${dir}/templates/init_slots.inja)
  cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL _rexfabric_finish)
endfunction()
