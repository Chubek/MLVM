# Shared by standalone component builds and add_subdirectory(libstone).
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/MLVMDependencies.cmake")
include(GNUInstallDirs)
include(CTest)

set(_libstone_tests_default "${BUILD_TESTING}")
set(_libstone_tools_default ON)
set(_libstone_warnings_default ON)
if(DEFINED MLVM_BUILD_TESTS)
  set(_libstone_tests_default "${MLVM_BUILD_TESTS}")
endif()
if(DEFINED MLVM_BUILD_TOOLS)
  set(_libstone_tools_default "${MLVM_BUILD_TOOLS}")
endif()
if(DEFINED MLVM_ENABLE_WARNINGS)
  set(_libstone_warnings_default "${MLVM_ENABLE_WARNINGS}")
endif()
option(LIBSTONE_BUILD_TESTS "Build libstone tests" "${_libstone_tests_default}")
option(LIBSTONE_BUILD_TOOLS "Build libstone command-line tools and examples" "${_libstone_tools_default}")
option(LIBSTONE_ENABLE_WARNINGS "Enable libstone compiler warnings" "${_libstone_warnings_default}")

function(libstone_require_files source_dir)
  foreach(file IN LISTS ARGN)
    if(NOT EXISTS "${${source_dir}}/${file}")
      message(FATAL_ERROR
        "Missing ${${source_dir}}/${file}. Run scripts/get-deps.sh from MLVM, "
        "or set ${source_dir} (dependency root: MLVM_THIRD_PARTY_DIR).")
    endif()
  endforeach()
endfunction()

function(libstone_warnings target)
  if(LIBSTONE_ENABLE_WARNINGS)
    target_compile_options(${target} PRIVATE
      $<$<COMPILE_LANG_AND_ID:C,MSVC>:/W4>
      $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/W4>
      $<$<COMPILE_LANG_AND_ID:C,GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic>
      $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic>)
  endif()
endfunction()
