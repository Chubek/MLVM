libstone_require_files(MLVM_LUA_SOURCE_DIR lua.h lapi.c lauxlib.c)
libstone_require_files(MLVM_KAGUYA_SOURCE_DIR include/kaguya/kaguya.hpp)
set(vmweave_lua_sources)
foreach(source IN ITEMS lapi lauxlib lbaselib lcode lcorolib lctype ldblib
    ldebug ldo ldump lfunc lgc linit liolib llex lmathlib lmem loadlib
    lobject lopcodes loslib lparser lstate lstring lstrlib ltable ltablib
    ltm lundump lutf8lib lvm lzio)
  list(APPEND vmweave_lua_sources "${MLVM_LUA_SOURCE_DIR}/${source}.c")
endforeach()
add_library(limestone_vmweave_lua STATIC ${vmweave_lua_sources})
set_target_properties(limestone_vmweave_lua PROPERTIES
  C_STANDARD 99 C_STANDARD_REQUIRED ON POSITION_INDEPENDENT_CODE ON)
target_include_directories(limestone_vmweave_lua SYSTEM PUBLIC
  $<BUILD_INTERFACE:${MLVM_LUA_SOURCE_DIR}>)
if(UNIX)
  target_compile_definitions(limestone_vmweave_lua PRIVATE LUA_USE_POSIX LUA_USE_DLOPEN)
  target_link_libraries(limestone_vmweave_lua PRIVATE m ${CMAKE_DL_LIBS})
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS vmweave.lua)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/vmweave.lua" VMWEAVE_LUA_SOURCE)
configure_file(lua_source.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/lua_source.hpp" @ONLY)
add_library(vmweave STATIC model.cpp frontend.cpp stk.cpp c_backend.cpp machineir.cpp native.cpp native_c_api.cpp)
target_link_libraries(vmweave PUBLIC limestone_foundation PRIVATE machineir_bridge machineir_native
  limestone_vmweave_lua)
target_include_directories(vmweave SYSTEM PRIVATE "${MLVM_KAGUYA_SOURCE_DIR}/include")
target_include_directories(vmweave PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
if(LIMESTONE_BUILD_TOOLS OR LIMESTONE_BUILD_TESTS)
  add_executable(vmweave-cli main.cpp)
  target_link_libraries(vmweave-cli PRIVATE vmweave)
endif()
if(LIMESTONE_BUILD_TESTS)
  add_executable(limestone-lua "${MLVM_LUA_SOURCE_DIR}/lua.c")
  target_link_libraries(limestone-lua PRIVATE limestone_vmweave_lua)
  add_executable(vmweave-model-test tests/model.cpp)
  target_link_libraries(vmweave-model-test PRIVATE vmweave machineir_bridge)
  add_test(NAME vmweave-model COMMAND vmweave-model-test)
  add_executable(vmweave-allocators-test tests/allocators.cpp)
  target_link_libraries(vmweave-allocators-test PRIVATE limestone_foundation)
  target_include_directories(vmweave-allocators-test SYSTEM PRIVATE "${MLVM_MEMTKX_SOURCE_DIR}/include")
  add_test(NAME vmweave-allocators COMMAND vmweave-allocators-test)
  set(native_fixtures "${CMAKE_CURRENT_BINARY_DIR}/native-fixtures")
  add_custom_command(OUTPUT "${native_fixtures}/Calculator.c" "${native_fixtures}/Calculator.h"
    "${native_fixtures}/ObjectVM.c" "${native_fixtures}/ObjectVM.h"
    "${native_fixtures}/Hooked.h"
    COMMAND vmweave-cli --generate -o "${native_fixtures}" "${CMAKE_CURRENT_SOURCE_DIR}/examples/calculator.lua"
    COMMAND vmweave-cli --generate -o "${native_fixtures}" "${CMAKE_CURRENT_SOURCE_DIR}/examples/objectvm.lua"
    COMMAND vmweave-cli --generate -o "${native_fixtures}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/native_hooks.lua"
    DEPENDS vmweave-cli examples/calculator.lua examples/objectvm.lua tests/native_hooks.lua VERBATIM)
  add_custom_target(vmweave-native-fixtures DEPENDS "${native_fixtures}/Calculator.c" "${native_fixtures}/ObjectVM.c")
  add_executable(vmweave-native-test tests/native.cpp "${native_fixtures}/Calculator.c")
  add_dependencies(vmweave-native-test vmweave-native-fixtures)
  target_include_directories(vmweave-native-test PRIVATE "${native_fixtures}")
  target_link_libraries(vmweave-native-test PRIVATE vmweave machineir_native)
  set_target_properties(vmweave-native-test PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED YES)
  add_test(NAME vmweave-native COMMAND vmweave-native-test "${CMAKE_CURRENT_SOURCE_DIR}")
  add_executable(vmweave-native-c-test tests/native_bridge.c)
  add_dependencies(vmweave-native-c-test vmweave-native-fixtures)
  target_include_directories(vmweave-native-c-test PRIVATE "${native_fixtures}")
  target_link_libraries(vmweave-native-c-test PRIVATE vmweave)
  set_target_properties(vmweave-native-c-test PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED YES LINKER_LANGUAGE CXX)
  target_compile_options(vmweave-native-c-test PRIVATE $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Werror>)
  add_test(NAME vmweave-native-c COMMAND vmweave-native-c-test)
  add_test(NAME vmweave-native-cli COMMAND ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:vmweave-cli> -DCC=${CMAKE_C_COMPILER}
    -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR} -DBINARY=${CMAKE_CURRENT_BINARY_DIR}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/native_cli.cmake)
  add_test(NAME vmweave-runtime COMMAND ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:vmweave-cli> -DCC=${CMAKE_C_COMPILER}
    -DKLIB=${MLVM_KLIB_SOURCE_DIR}
    -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR} -DBINARY=${CMAKE_CURRENT_BINARY_DIR}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime.cmake)
endif()
