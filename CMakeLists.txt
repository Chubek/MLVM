cmake_minimum_required(VERSION 3.20)
project(MLVM VERSION 0.1.0 LANGUAGES C CXX)

option(MLVM_BUILD_TOOLS "Build MLVM command-line tools" ON)
option(MLVM_BUILD_TESTS "Build MLVM tests" ON)
option(MLVM_ENABLE_WARNINGS "Enable strict compiler warnings" ON)
set(MLVM_ENABLE_CEPHYR AUTO CACHE STRING "Build Cephyr: AUTO, ON, or OFF")
set_property(CACHE MLVM_ENABLE_CEPHYR PROPERTY STRINGS AUTO ON OFF)
if(NOT MLVM_ENABLE_CEPHYR MATCHES "^(AUTO|ON|OFF)$")
  message(FATAL_ERROR "MLVM_ENABLE_CEPHYR must be AUTO, ON, or OFF")
endif()
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(MLVMDependencies)
# Requested state before per-dependency modules resolve AUTO. Modules fail
# closed on ON and disable on AUTO; neither may mistake the other's resolved
# ON for an explicit request.
set(MLVM_CEPHYR_WANT_ON OFF)
if(MLVM_ENABLE_CEPHYR STREQUAL "ON")
  set(MLVM_CEPHYR_WANT_ON ON)
endif()
include(MLVMCParser)
include(MLVMUcpp)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)
include(CTest)

function(mlvm_warnings target)
  if(MLVM_ENABLE_WARNINGS)
    if(MSVC)
      target_compile_options(${target} PRIVATE /W4)
    else()
      target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
  endif()
endfunction()

add_library(mlvm_nodeir INTERFACE)
add_library(MLVM::NodeIR ALIAS mlvm_nodeir)
target_compile_features(mlvm_nodeir INTERFACE cxx_std_20)
target_include_directories(mlvm_nodeir INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
set_target_properties(mlvm_nodeir PROPERTIES EXPORT_NAME NodeIR)

add_library(mlvm_core src/core/core.cpp src/core/c_api.cpp)
add_library(MLVM::Core ALIAS mlvm_core)
target_include_directories(mlvm_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(mlvm_core PUBLIC cxx_std_20)
set_target_properties(mlvm_core PROPERTIES EXPORT_NAME Core)
mlvm_warnings(mlvm_core)

add_library(mlvm_ir src/ir/ir.cpp src/ir/text.cpp src/dialect/builtin.cpp src/dialect/c.cpp src/dialect/dialects.cpp
  src/pass/pass.cpp src/conversion/conversion.cpp)
add_library(MLVM::IR ALIAS mlvm_ir)
target_link_libraries(mlvm_ir PUBLIC MLVM::Core)
set_target_properties(mlvm_ir PROPERTIES EXPORT_NAME IR)
mlvm_warnings(mlvm_ir)

add_library(mlvm_codegen src/codegen/x86_64.cpp src/codegen/c.cpp src/target/target.cpp src/target/emit.cpp)
add_library(MLVM::Codegen ALIAS mlvm_codegen)
target_link_libraries(mlvm_codegen PUBLIC MLVM::IR)
set_target_properties(mlvm_codegen PROPERTIES EXPORT_NAME Codegen)
mlvm_warnings(mlvm_codegen)

add_library(mlvm_object src/object/elf.cpp src/object/archive.cpp
  src/assembler/assembler.cpp src/linker/linker.cpp)
add_library(MLVM::Object ALIAS mlvm_object)
target_link_libraries(mlvm_object PUBLIC MLVM::Codegen)
set_target_properties(mlvm_object PROPERTIES EXPORT_NAME Object)
mlvm_warnings(mlvm_object)

include(MLVMBinutils)
include(MLVMScheduler)

if(MLVM_ENABLE_CEPHYR)
  add_library(cephyr_frontend cephyr/src/preprocess/preprocess.cpp cephyr/src/preprocess/ucpp.cpp
    cephyr/src/preprocess/system.cpp cephyr/src/parse/frontend.cpp
    cephyr/src/sema/types.cpp cephyr/src/sema/lower.cpp cephyr/src/lower/codegen.cpp
    cephyr/src/sched/actions.cpp)
  add_library(MLVM::Cephyr ALIAS cephyr_frontend)
  target_include_directories(cephyr_frontend PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/cephyr/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
  target_link_libraries(cephyr_frontend PUBLIC MLVM::IR MLVM::Scheduler PRIVATE mlvm_cparser mlvm_ucpp MLVM::Codegen)
  set_target_properties(cephyr_frontend PROPERTIES EXPORT_NAME Cephyr)
  mlvm_warnings(cephyr_frontend)
  install(TARGETS cephyr_frontend mlvm_cparser mlvm_ucpp EXPORT MLVMTargets)
  install(FILES "${MLVM_CPARSER_SOURCE_DIR}/LICENSE"
    DESTINATION ${CMAKE_INSTALL_DOCDIR}/licenses RENAME cparser-iso11.txt)
  install(FILES "${MLVM_UCPP_SOURCE_DIR}/LICENSE"
    DESTINATION ${CMAKE_INSTALL_DOCDIR}/licenses RENAME ucpp.txt)
  install(DIRECTORY cephyr/include/cephyr DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

  # Cephyr ships cephyr-libc as its freestanding C library. It is the default
  # sysroot; configure with -DMLVM_CEPHYR_LIBC=OFF to build Cephyr against a
  # different C library instead.
  option(MLVM_CEPHYR_LIBC "Use the bundled cephyr/libc as Cephyr's C library" ON)
  set(MLVM_CEPHYR_SYSROOT "" CACHE PATH "External Cephyr sysroot (include/, lib/crt0.o, lib/libc.a); overrides bundled libc")
  set(CEPHYR_BUILD_SYSROOT "")
  if(MLVM_CEPHYR_LIBC AND NOT MLVM_CEPHYR_SYSROOT)
    set(CEPHYR_BUILD_SYSROOT "${CMAKE_CURRENT_BINARY_DIR}/cephyr-sysroot")
    add_subdirectory(cephyr/libc ${CMAKE_CURRENT_BINARY_DIR}/cephyr-libc)
    install(DIRECTORY cephyr/libc/include/ DESTINATION ${CMAKE_INSTALL_DATADIR}/mlvm/cephyr-libc/include)
    install(TARGETS cephyr_libc EXPORT MLVMTargets
      ARCHIVE DESTINATION ${CMAKE_INSTALL_DATADIR}/mlvm/cephyr-libc/lib)
  else()
    message(STATUS "Cephyr built against an external C library")
  endif()
  file(RELATIVE_PATH CEPHYR_RELATIVE_SYSROOT "${CMAKE_INSTALL_FULL_BINDIR}"
    "${CMAKE_INSTALL_FULL_DATADIR}/mlvm/cephyr-libc")
  target_compile_definitions(cephyr_frontend PRIVATE
    MLVM_CEPHYR_EXTERNAL_SYSROOT="${MLVM_CEPHYR_SYSROOT}"
    MLVM_CEPHYR_BUILD_SYSROOT="${CEPHYR_BUILD_SYSROOT}"
    MLVM_CEPHYR_RELATIVE_SYSROOT="${CEPHYR_RELATIVE_SYSROOT}"
    MLVM_CEPHYR_INSTALL_SYSROOT="${CMAKE_INSTALL_FULL_DATADIR}/mlvm/cephyr-libc")
endif()

if(MLVM_BUILD_TOOLS)
  add_library(mlvm_driver STATIC src/driver/driver.cpp)
  target_link_libraries(mlvm_driver PUBLIC MLVM::IR MLVM::Object MLVM::Scheduler)
  mlvm_warnings(mlvm_driver)
  if(MLVM_ENABLE_CEPHYR)
    target_link_libraries(mlvm_driver PRIVATE MLVM::Cephyr)
    target_compile_definitions(mlvm_driver PRIVATE MLVM_HAS_CEPHYR=1)
  endif()
  function(mlvm_add_tool target display_name)
    add_executable(${target} tools/common/main.cpp)
    target_link_libraries(${target} PRIVATE mlvm_driver)
    target_include_directories(${target} PRIVATE src/driver)
    target_compile_definitions(${target} PRIVATE MLVM_TOOL_NAME="${display_name}")
    mlvm_warnings(${target})
    install(TARGETS ${target} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  endfunction()

  foreach(tool mlvm mlvm-as mlvm-sched)
    mlvm_add_tool(${tool} ${tool})
  endforeach()
  if(MLVM_ENABLE_BINUTILS)
    add_library(mlvm_tool_support STATIC tools/common/cli.cpp tools/common/inspect.cpp)
    target_include_directories(mlvm_tool_support PUBLIC tools/common PRIVATE ${MLVM_KLIB_SOURCE_DIR})
    target_link_libraries(mlvm_tool_support PUBLIC MLVM::Binutils)
    mlvm_warnings(mlvm_tool_support)
  endif()
  foreach(tool mlvm-ld mlvm-ar mlvm-nm mlvm-objdump mlvm-objcopy
               mlvm-readobj mlvm-readelf mlvm-strip mlvm-size mlvm-strings)
    if(MLVM_ENABLE_BINUTILS)
      add_executable(${tool} tools/${tool}/main.cpp)
      target_link_libraries(${tool} PRIVATE mlvm_tool_support)
      if(tool STREQUAL "mlvm-ld")
        target_link_libraries(${tool} PRIVATE mlvm_driver)
        target_include_directories(${tool} PRIVATE src/driver)
      endif()
      mlvm_warnings(${tool})
      install(TARGETS ${tool} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    else()
      mlvm_add_tool(${tool} ${tool})
    endif()
  endforeach()
  if(MLVM_ENABLE_CEPHYR)
    mlvm_add_tool(cephyr cephyr)
    if(TARGET cephyr_libc)
      add_dependencies(cephyr cephyr_libc cephyr_crt0)
    endif()
  endif()
endif()

if(MLVM_BUILD_TESTS)
  enable_testing()
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    add_test(NAME mlvm-get-deps COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/get_deps.py)
  endif()
  add_executable(mlvm-smoke-test tests/smoke.cpp)
  target_link_libraries(mlvm-smoke-test PRIVATE MLVM::Core)
  add_test(NAME mlvm-smoke-test COMMAND mlvm-smoke-test)
  foreach(test core/core core/ir pass/pass conversion/conversion object)
    string(REPLACE "/" "-" test_name "${test}")
    add_executable(mlvm-test-${test_name} tests/${test}.cpp)
    target_link_libraries(mlvm-test-${test_name} PRIVATE MLVM::IR MLVM::Object)
    mlvm_warnings(mlvm-test-${test_name})
    add_test(NAME mlvm-${test_name} COMMAND mlvm-test-${test_name})
  endforeach()
  add_executable(mlvm-test-c-api tests/core/c_api.c)
  target_link_libraries(mlvm-test-c-api PRIVATE MLVM::Core)
  add_test(NAME mlvm-c-api COMMAND mlvm-test-c-api)
  add_executable(mlvm-test-sched tests/sched.cpp)
  target_link_libraries(mlvm-test-sched PRIVATE MLVM::Scheduler)
  target_compile_definitions(mlvm-test-sched PRIVATE MLVM_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    MLVM_TEST_BINARY_DIR="${CMAKE_CURRENT_BINARY_DIR}")
  mlvm_warnings(mlvm-test-sched)
  add_test(NAME mlvm-sched COMMAND mlvm-test-sched)
  add_executable(mlvm-test-blackstone-sched tests/blackstone_sched.cpp)
  target_link_libraries(mlvm-test-blackstone-sched PRIVATE MLVM::Scheduler)
  target_compile_definitions(mlvm-test-blackstone-sched PRIVATE MLVM_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    MLVM_TEST_BINARY_DIR="${CMAKE_CURRENT_BINARY_DIR}")
  mlvm_warnings(mlvm-test-blackstone-sched)
  add_test(NAME mlvm-blackstone-sched COMMAND mlvm-test-blackstone-sched)
  if(MLVM_ENABLE_BINUTILS)
    add_executable(mlvm-test-binutils tests/binutils.cpp)
    target_link_libraries(mlvm-test-binutils PRIVATE MLVM::Binutils)
    mlvm_warnings(mlvm-test-binutils)
    add_test(NAME mlvm-binutils COMMAND mlvm-test-binutils)
  endif()
  if(MLVM_ENABLE_CEPHYR)
    add_executable(cephyr-test-frontend cephyr/tests/frontend.cpp)
    target_link_libraries(cephyr-test-frontend PRIVATE MLVM::Cephyr)
    mlvm_warnings(cephyr-test-frontend)
    add_test(NAME cephyr-frontend COMMAND cephyr-test-frontend)
    add_executable(cephyr-test-sched cephyr/tests/sched_actions.cpp)
    target_link_libraries(cephyr-test-sched PRIVATE MLVM::Cephyr)
    mlvm_warnings(cephyr-test-sched)
    add_test(NAME cephyr-sched-actions COMMAND cephyr-test-sched)
  endif()
  if(MLVM_BUILD_TOOLS)
    foreach(tool mlvm mlvm-as mlvm-sched mlvm-ld mlvm-ar mlvm-nm mlvm-objdump mlvm-objcopy
                 mlvm-readobj mlvm-readelf mlvm-strip mlvm-size mlvm-strings cephyr)
      if(TARGET ${tool})
        add_test(NAME ${tool}-help COMMAND ${tool} --help)
        add_test(NAME ${tool}-version COMMAND ${tool} --version)
      endif()
    endforeach()
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
      add_test(NAME mlvm-sched-cli COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/sched.py $<TARGET_FILE:mlvm-sched> ${CMAKE_CURRENT_SOURCE_DIR})
      add_test(NAME mlvm-toolchain COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/toolchain.py
        $<TARGET_FILE_DIR:mlvm> ${MLVM_ENABLE_CEPHYR})
      if(MLVM_ENABLE_CEPHYR)
        add_test(NAME cephyr-execute COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_CURRENT_SOURCE_DIR}/cephyr/tests/execute.py $<TARGET_FILE_DIR:cephyr>)
        add_test(NAME cephyr-driver COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_CURRENT_SOURCE_DIR}/cephyr/tests/driver.py $<TARGET_FILE_DIR:cephyr>)
        add_test(NAME cephyr-sched-cli COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_CURRENT_SOURCE_DIR}/cephyr/tests/sched.py $<TARGET_FILE_DIR:cephyr>)
      endif()
      if(MLVM_ENABLE_BINUTILS)
        add_test(NAME mlvm-binutils-cli COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/binutils.py
          $<TARGET_FILE_DIR:mlvm>)
      endif()
    endif()
  endif()
endif()

include(MLVMBlackstone)

option(MLVM_BUILD_LIBSTONE "Build the full libstone libraries and tools" OFF)
if(MLVM_BUILD_LIBSTONE)
  add_subdirectory(libstone)
endif()

set(MLVM_ENABLE_OPTILISP OFF CACHE STRING "Build legacy Optilisp when its sources are available")
set_property(CACHE MLVM_ENABLE_OPTILISP PROPERTY STRINGS AUTO ON OFF)
if(NOT MLVM_ENABLE_OPTILISP MATCHES "^(AUTO|ON|OFF)$")
  message(FATAL_ERROR "MLVM_ENABLE_OPTILISP must be AUTO, ON, or OFF")
endif()
set(MLVM_HAS_OPTILISP OFF)
if(NOT MLVM_ENABLE_OPTILISP STREQUAL "OFF")
  if(MLVM_SFSEXP_AVAILABLE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/optilisp/CMakeLists.txt")
    add_subdirectory(optilisp)
    set(MLVM_HAS_OPTILISP ON)
  elseif(MLVM_ENABLE_OPTILISP STREQUAL "ON")
    message(FATAL_ERROR "Legacy Optilisp requires optilisp/CMakeLists.txt and supplied sfsexp sources; use MLVM::Scheduler for native Blackstone kernels")
  else()
    message(STATUS "Legacy Optilisp disabled: its sources or sfsexp are unavailable")
  endif()
endif()

if(MLVM_HAS_OPTILISP AND TARGET mlvm_driver)
  target_link_libraries(mlvm_driver PRIVATE MLVM::OptilispPass)
  target_compile_definitions(mlvm_driver PRIVATE MLVM_HAS_OPTILISP=1
    MLVM_BLACKSTONE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/libstone/blackstone"
    MLVM_BLACKSTONE_INSTALL_DIR="${CMAKE_INSTALL_FULL_DATADIR}/mlvm/blackstone")
endif()
if(MLVM_HAS_OPTILISP)
  install(DIRECTORY libstone/blackstone/kernels libstone/blackstone/optilisp/stdlib
    DESTINATION ${CMAKE_INSTALL_DATADIR}/mlvm/blackstone)
  if(MLVM_BUILD_TESTS AND TARGET mlvm AND Python3_Interpreter_FOUND)
    add_test(NAME mlvm-kernels-cli COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/kernels.py $<TARGET_FILE:mlvm>)
    if(TARGET cephyr)
      add_test(NAME cephyr-kernels COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/cephyr/tests/kernels.py $<TARGET_FILE:cephyr>)
    endif()
  endif()
endif()

include(CMakePackageConfigHelpers)
configure_package_config_file(cmake/MLVMConfig.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/MLVMConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/MLVM)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/MLVMConfigVersion.cmake
  VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(TARGETS mlvm_nodeir mlvm_core mlvm_ir mlvm_codegen mlvm_object EXPORT MLVMTargets)
install(EXPORT MLVMTargets NAMESPACE MLVM:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/MLVM)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/MLVMConfig.cmake ${CMAKE_CURRENT_BINARY_DIR}/MLVMConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/MLVM)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY manual/ DESTINATION ${CMAKE_INSTALL_DOCDIR}/manual)
install(DIRECTORY docs/ DESTINATION ${CMAKE_INSTALL_DOCDIR})
