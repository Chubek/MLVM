# Build the supplied Stone runtimes without their standalone tools/test suites.
set(MLVM_LUA_SOURCE_DIR "${MLVM_THIRD_PARTY_DIR}/lua" CACHE PATH "Lua sources")
set(MLVM_KAGUYA_SOURCE_DIR "${MLVM_THIRD_PARTY_DIR}/kaguya" CACHE PATH "Kaguya sources")
set(MLVM_DPARSER_SOURCE_DIR "${MLVM_THIRD_PARTY_DIR}/dparser" CACHE PATH "DParser sources")
set(stone "${CMAKE_CURRENT_SOURCE_DIR}/libstone")
foreach(file "${MLVM_LUA_SOURCE_DIR}/lua.h" "${MLVM_KAGUYA_SOURCE_DIR}/include/kaguya/kaguya.hpp"
    "${MLVM_DPARSER_SOURCE_DIR}/dparse.h" "${stone}/blackstone/include/Blackstone-Rewrite.h"
    "${stone}/firestone/libir/RawDAG.hpp")
  if(NOT EXISTS "${file}")
    message(FATAL_ERROR "MLVM scheduler requires supplied dependency: ${file}")
  endif()
endforeach()

file(GLOB lua_sources CONFIGURE_DEPENDS "${MLVM_LUA_SOURCE_DIR}/*.c")
list(REMOVE_ITEM lua_sources "${MLVM_LUA_SOURCE_DIR}/lua.c" "${MLVM_LUA_SOURCE_DIR}/luac.c"
  "${MLVM_LUA_SOURCE_DIR}/onelua.c" "${MLVM_LUA_SOURCE_DIR}/ltests.c")
add_library(mlvm_lua STATIC ${lua_sources})
target_include_directories(mlvm_lua PUBLIC $<BUILD_INTERFACE:${MLVM_LUA_SOURCE_DIR}>)
if(UNIX)
  target_compile_definitions(mlvm_lua PRIVATE LUA_USE_POSIX LUA_USE_DLOPEN)
  target_link_libraries(mlvm_lua PUBLIC ${CMAKE_DL_LIBS} m)
endif()

set(D "${MLVM_DPARSER_SOURCE_DIR}")
add_library(mlvm_dparse STATIC ${D}/arg.c ${D}/parse.c ${D}/scan.c ${D}/dsymtab.c
  ${D}/util.c ${D}/read_binary.c ${D}/dparse_tree.c ${D}/version.c)
target_include_directories(mlvm_dparse PUBLIC $<BUILD_INTERFACE:${D}>)
target_compile_definitions(mlvm_dparse PRIVATE D_MAJOR_VERSION=1 D_MINOR_VERSION=30)
add_executable(mlvm_make_dparser ${D}/make_dparser.c ${D}/mkdparse.c ${D}/write_tables.c
  ${D}/grammar.g.c ${D}/gram.c ${D}/lex.c ${D}/lr.c)
target_link_libraries(mlvm_make_dparser PRIVATE mlvm_dparse)
set(tables "${CMAKE_CURRENT_BINARY_DIR}/blackstone.g.c")
add_custom_command(OUTPUT "${tables}"
  COMMAND mlvm_make_dparser "${stone}/blackstone/grammar/blackstone.g" -i mlvm_blackstone -o "${tables}"
  DEPENDS mlvm_make_dparser "${stone}/blackstone/grammar/blackstone.g" VERBATIM)

set(B "${stone}/blackstone")
add_library(mlvm_blackstone STATIC ${B}/src/api.c ${B}/src/profile.c ${B}/src/bincode.c
  ${B}/src/driver.c ${B}/concern/module.c ${B}/concern/loader.c ${B}/concern/apply.c
  ${B}/requires/module.c ${B}/requires/loader.c ${B}/requires/apply.c
  ${B}/rewrite/pattern.c ${B}/rewrite/match.c ${B}/rewrite/mmap.c
  ${B}/stdlib/concerns/HIR/Loops/Descriptor.c)
add_library(MLVM::Blackstone ALIAS mlvm_blackstone)
target_include_directories(mlvm_blackstone PUBLIC
  $<BUILD_INTERFACE:${B}/include> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/blackstone>
  PRIVATE ${B}/src ${B}/profile)
set_target_properties(mlvm_blackstone PROPERTIES EXPORT_NAME Blackstone)

add_library(mlvm_firestone INTERFACE)
find_package(Threads REQUIRED)
target_link_libraries(mlvm_firestone INTERFACE Threads::Threads)
add_library(MLVM::Firestone ALIAS mlvm_firestone)
target_include_directories(mlvm_firestone INTERFACE
  $<BUILD_INTERFACE:${stone}/firestone> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/firestone>)
target_compile_features(mlvm_firestone INTERFACE cxx_std_20)
set_target_properties(mlvm_firestone PROPERTIES EXPORT_NAME Firestone)

add_library(mlvm_sched src/sched/sched_graph.cpp src/sched/sched.cpp src/sched/loader.cpp
  src/sched/lua.cpp src/sched/blackstone.cpp src/sched/firestone.cpp ${tables})
add_library(MLVM::Scheduler ALIAS mlvm_sched)
target_link_libraries(mlvm_sched PUBLIC MLVM::IR MLVM::Object MLVM::Firestone MLVM::Blackstone
  PRIVATE mlvm_lua mlvm_dparse)
target_include_directories(mlvm_sched SYSTEM PRIVATE "${MLVM_KAGUYA_SOURCE_DIR}/include")
set_target_properties(mlvm_sched PROPERTIES EXPORT_NAME Scheduler)
mlvm_warnings(mlvm_sched)
foreach(target mlvm_lua mlvm_dparse mlvm_blackstone)
  set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
endforeach()
install(TARGETS mlvm_sched mlvm_blackstone mlvm_firestone mlvm_lua mlvm_dparse EXPORT MLVMTargets)
install(DIRECTORY ${B}/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/blackstone)
install(DIRECTORY ${stone}/firestone/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/firestone
  FILES_MATCHING PATTERN "*.hpp" PATTERN tests EXCLUDE)
install(DIRECTORY ${B}/kernels DESTINATION ${CMAKE_INSTALL_DATADIR}/mlvm/blackstone)
file(GLOB orchestrators CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/examples/sched/*.sched")
foreach(orchestrator IN LISTS orchestrators)
  get_filename_component(name "${orchestrator}" NAME)
  file(READ "${orchestrator}" source)
  string(REPLACE "../../libstone/blackstone/" "../../blackstone/" source "${source}")
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/examples/sched/${name}" "${source}")
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/examples/sched/${name}" DESTINATION ${CMAKE_INSTALL_DATADIR}/mlvm/examples/sched)
endforeach()
install(FILES examples/sched/input.mlvm examples/sched/README.md DESTINATION ${CMAKE_INSTALL_DATADIR}/mlvm/examples/sched)
