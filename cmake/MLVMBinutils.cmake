# Build only the library sources needed by the adapter. This deliberately
# avoids configuring libstone's independent CLIs, generators and dependencies.
# External deps: third_party/klib (ketopt), third_party/tomlc99 (toml)
# (see get-deps.sh). Limestone itself is checked in under libstone/limestone.
set(MLVM_ENABLE_BINUTILS AUTO CACHE STRING "Library-backed binary tools: AUTO, ON, or OFF")
set_property(CACHE MLVM_ENABLE_BINUTILS PROPERTY STRINGS AUTO ON OFF)
if(NOT MLVM_ENABLE_BINUTILS MATCHES "^(AUTO|ON|OFF)$")
  message(FATAL_ERROR "MLVM_ENABLE_BINUTILS must be AUTO, ON, or OFF")
endif()
if(NOT MLVM_ENABLE_BINUTILS STREQUAL "OFF")
  set(MLVM_KLIB_SOURCE_DIR "${MLVM_THIRD_PARTY_DIR}/klib"
    CACHE PATH "klib sources (populated by get-deps.sh)")
  set(MLVM_TOML_SOURCE_DIR "${MLVM_THIRD_PARTY_DIR}/tomlc99"
    CACHE PATH "tomlc99 sources (populated by get-deps.sh)")

  foreach(source libstone/limestone/bin2bin/object.cpp libstone/limestone/bin2bin/archive.cpp
                 libstone/limestone/bin2bin/object.hpp libstone/limestone/metacode/metacode.hpp
                 libstone/limestone/limestone/foundation.hpp
                 "${MLVM_KLIB_SOURCE_DIR}/ketopt.h"
                 "${MLVM_TOML_SOURCE_DIR}/toml.c" "${MLVM_TOML_SOURCE_DIR}/toml.h")
    if(NOT EXISTS "${source}")
      if(MLVM_ENABLE_BINUTILS STREQUAL "ON")
        message(FATAL_ERROR "Library-backed binary tools require ${source}")
      endif()
      set(MLVM_ENABLE_BINUTILS OFF)
      message(STATUS "Library-backed binary tools disabled: ${source} unavailable")
      break()
    endif()
  endforeach()
endif()
if(NOT MLVM_ENABLE_BINUTILS STREQUAL "OFF")
  set(MLVM_ENABLE_BINUTILS ON)
  add_library(mlvm_stone_objects STATIC libstone/limestone/bin2bin/object.cpp
    libstone/limestone/bin2bin/archive.cpp)
  target_include_directories(mlvm_stone_objects PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/libstone/limestone>)
  target_compile_features(mlvm_stone_objects PUBLIC cxx_std_20)
  set_target_properties(mlvm_stone_objects PROPERTIES EXPORT_NAME StoneObjects POSITION_INDEPENDENT_CODE ON)
  add_library(mlvm_toml STATIC "${MLVM_TOML_SOURCE_DIR}/toml.c")
  target_include_directories(mlvm_toml PUBLIC
    $<BUILD_INTERFACE:${MLVM_TOML_SOURCE_DIR}>)
  set_target_properties(mlvm_toml PROPERTIES C_STANDARD 99 EXPORT_NAME TOML POSITION_INDEPENDENT_CODE ON)
  add_library(mlvm_binutils src/binutils/adapter.cpp src/binutils/archive.cpp
    src/binutils/contract.cpp src/binutils/transform.cpp)
  add_library(MLVM::Binutils ALIAS mlvm_binutils)
  target_link_libraries(mlvm_binutils PUBLIC MLVM::Object PRIVATE mlvm_stone_objects mlvm_toml)
  set_target_properties(mlvm_binutils PROPERTIES EXPORT_NAME Binutils)
  mlvm_warnings(mlvm_binutils)
  install(TARGETS mlvm_binutils mlvm_stone_objects mlvm_toml EXPORT MLVMTargets)
  install(FILES libstone/limestone/LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR}/licenses RENAME Limestone-LICENSE)
  install(FILES "${MLVM_TOML_SOURCE_DIR}/LICENSE" DESTINATION ${CMAKE_INSTALL_DOCDIR}/licenses RENAME tomlc99-LICENSE)
  install(FILES "${MLVM_KLIB_SOURCE_DIR}/LICENSE.txt" DESTINATION ${CMAKE_INSTALL_DOCDIR}/licenses RENAME klib-LICENSE)
  install(DIRECTORY examples/targets/ DESTINATION ${CMAKE_INSTALL_DATADIR}/mlvm/targets)
endif()
