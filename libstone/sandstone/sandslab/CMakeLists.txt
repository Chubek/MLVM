cmake_minimum_required(VERSION 3.20)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(SandSLAB VERSION 1.0.0 LANGUAGES CXX)
endif()
include(../../cmake/LibstoneBuild.cmake)
option(SANDSTONE_BUILD_TESTS "Build and register tests" "${LIBSTONE_BUILD_TESTS}")
option(SANDSTONE_BUILD_TOOLS "Build Sandstone command-line tools" "${LIBSTONE_BUILD_TOOLS}")
if(SANDSTONE_BUILD_TESTS)
  enable_testing()
endif()

libstone_require_files(MLVM_METATK_SOURCE_DIR DSLtk/DSLtk.hpp)

add_library(sandslab INTERFACE)
add_library(Sandstone::sandslab ALIAS sandslab)
target_compile_features(sandslab INTERFACE cxx_std_20)
target_include_directories(sandslab INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../isa>
  $<BUILD_INTERFACE:${MLVM_METATK_SOURCE_DIR}/DSLtk>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/sandslab>)

if(SANDSTONE_BUILD_TOOLS)
  add_executable(forthcc examples/FORTHCC.cpp)
  target_link_libraries(forthcc PRIVATE sandslab)
  target_compile_definitions(forthcc PRIVATE
    SANDSLAB_ISA_DIR="${CMAKE_INSTALL_FULL_DATADIR}/sandstone/isa")
  install(TARGETS forthcc RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

if(SANDSTONE_BUILD_TESTS)
  add_executable(sandslab_test tests.cpp)
  target_link_libraries(sandslab_test PRIVATE sandslab)
  target_compile_definitions(sandslab_test PRIVATE
    SANDSLAB_TEST_ISA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../isa")
  add_test(NAME sandslab COMMAND sandslab_test)
endif()

install(TARGETS sandslab EXPORT SandSLABTargets)
install(EXPORT SandSLABTargets FILE SandSLABConfig.cmake NAMESPACE Sandstone::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SandSLAB)
install(FILES SandSLAB.hpp ../isa/simple_isa.hpp "${MLVM_METATK_SOURCE_DIR}/DSLtk/DSLtk.hpp"
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/sandslab)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  install(DIRECTORY ../isa/ DESTINATION ${CMAKE_INSTALL_DATADIR}/sandstone/isa
    FILES_MATCHING PATTERN "*.isa")
endif()
