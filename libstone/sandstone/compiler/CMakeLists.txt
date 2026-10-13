# Sandstone spec compiler: JSON spec documents -> SymAS (.symas) banks.
#
# Layers:
#   sandstone_spec_core     JSON spec document -> domsymas::Builder (no Lua)
#   sandstone-spec-compiler dependency-free CLI driver (always built)
#   domsymascc              legacy DomMune-embedded CLI (main.cpp), built only
#                           when SANDSTONE_WITH_MUNE is ON and the DomMune /
#                           DomCLI sources are provided (they are not part of
#                           this repository).
#
# The .dsymspec -> JSON step is performed by scripts/spec-compiler.py with
# stock Lua (the dsl.lua DSL is Lua 5.4 compatible); the JSON -> .symas step
# is this directory.

option(SANDSTONE_WITH_MUNE "Build the legacy DomMune-embedded domsymascc CLI" OFF)

# ---- spec core (JSON document -> Builder), no interpreter required ----
add_library(sandstone_spec_core STATIC
  src/spec_document.cpp
)
target_include_directories(sandstone_spec_core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(sandstone_spec_core PUBLIC sandstone)
libstone_warnings(sandstone_spec_core)
# Backwards-compatible alias for the historical target name.
add_library(domsymascc_core ALIAS sandstone_spec_core)

# ---- dependency-free CLI ----
if(SANDSTONE_BUILD_TOOLS OR SANDSTONE_BUILD_TESTS)
  add_executable(sandstone-spec-compiler src/spec_compiler.cpp)
  target_link_libraries(sandstone-spec-compiler PRIVATE sandstone_spec_core)
  libstone_warnings(sandstone-spec-compiler)
endif()

if(SANDSTONE_BUILD_TOOLS)
  install(TARGETS sandstone-spec-compiler RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
install(TARGETS sandstone_spec_core
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(FILES include/domsymascc/domsymascc.hpp
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/domsymascc
)
install(DIRECTORY stdspec/
  DESTINATION ${CMAKE_INSTALL_DATADIR}/sandstone/stdspec
  FILES_MATCHING PATTERN "*.dsymspec" PATTERN "*.lua" PATTERN "README.md"
)

# ---- legacy DomMune-embedded driver (optional, needs external sources) ----
if(SANDSTONE_WITH_MUNE)
  set(SANDSTONE_MUNE_SOURCE_DIR "${MLVM_THIRD_PARTY_DIR}/dommune" CACHE PATH "Optional DomMune sources")
  set(SANDSTONE_DOMCLI_SOURCE_DIR "${MLVM_THIRD_PARTY_DIR}/domcli" CACHE PATH "Optional DomCLI headers")
  if(NOT TARGET mune_core)
    libstone_require_files(SANDSTONE_MUNE_SOURCE_DIR CMakeLists.txt)
    add_subdirectory("${SANDSTONE_MUNE_SOURCE_DIR}"
                     "${CMAKE_CURRENT_BINARY_DIR}/dommune" EXCLUDE_FROM_ALL)
  endif()
  add_executable(domsymascc src/main.cpp)
  target_include_directories(domsymascc PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${SANDSTONE_DOMCLI_SOURCE_DIR}
  )
  target_link_libraries(domsymascc PRIVATE sandstone_spec_core mune_core)
  libstone_warnings(domsymascc)
  install(TARGETS domsymascc RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
