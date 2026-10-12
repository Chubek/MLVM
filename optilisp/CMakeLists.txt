# Optilisp: sfsexp-backed S-expression loader and SSA integer pass.
#
# Provides MLVM::OptilispPass implementing <mlvm/optilisp/*.hpp>:
# value-owned S-expression parsing (sexpr), kernel registry/loading with
# strict and blackstone profiles (loader), executable tree rewriting
# (rewrite), and the MLVM Module pass bridge (pass).
#
# sfsexp sources come from MLVM_THIRD_PARTY_DIR/sfsexp (see get-deps.sh) and
# are compiled without configuring the dependency tree. The public headers
# expose only MLVM-owned types; no sfsexp pointers escape the parser.

set(MLVM_OPTILISP_EXPORT MLVMTargets)

# Reuse the shared sfsexp build when available, otherwise define it here so
# a standalone -DMLVM_ENABLE_OPTILISP=ON configure still succeeds.
if(NOT TARGET mlvm_sfsexp)
  include(MLVMSfsexp)
endif()

add_library(mlvm_optilisp STATIC
  src/sexpr.cpp
  src/loader.cpp
  src/rewrite.cpp
  src/pass.cpp
)
add_library(MLVM::OptilispPass ALIAS mlvm_optilisp)

target_include_directories(mlvm_optilisp PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(mlvm_optilisp PUBLIC MLVM::IR PRIVATE mlvm_sfsexp)
target_compile_features(mlvm_optilisp PUBLIC cxx_std_20)
set_target_properties(mlvm_optilisp PROPERTIES
  EXPORT_NAME OptilispPass
  POSITION_INDEPENDENT_CODE ON
)

mlvm_warnings(mlvm_optilisp)

install(TARGETS mlvm_optilisp EXPORT ${MLVM_OPTILISP_EXPORT})

if(MLVM_BUILD_TESTS)
  add_executable(mlvm-test-optilisp tests/optilisp.cpp)
  target_link_libraries(mlvm-test-optilisp PRIVATE MLVM::OptilispPass MLVM::IR)
  target_include_directories(mlvm-test-optilisp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../tests)
  target_compile_definitions(mlvm-test-optilisp PRIVATE
    MLVM_TEST_BLACKSTONE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../libstone/blackstone")
  mlvm_warnings(mlvm-test-optilisp)
  add_test(NAME mlvm-optilisp COMMAND mlvm-test-optilisp)
endif()
