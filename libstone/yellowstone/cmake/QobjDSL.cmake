# Yellowstone's compatibility target uses the shared MetaTk DSL toolkit.
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/LibstoneBuild.cmake")
set(qobjfile_qdsl_SOURCE "${MLVM_METATK_SOURCE_DIR}/DSLtk")
if(NOT TARGET qobjfile::qdsl)
  libstone_require_files(MLVM_METATK_SOURCE_DIR DSLtk/DSLtk.hpp)
  add_library(qobjfile_qdsl INTERFACE)
  target_compile_features(qobjfile_qdsl INTERFACE cxx_std_20)
  target_include_directories(qobjfile_qdsl SYSTEM INTERFACE
    $<BUILD_INTERFACE:${MLVM_METATK_SOURCE_DIR}/DSLtk>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/qobjfile>)
  add_library(qobjfile::qdsl ALIAS qobjfile_qdsl)
endif()
