# Resolve dependencies relative to this file so standalone and composed builds
# share MLVM's local third_party tree, independent of CMAKE_SOURCE_DIR.
# Populate it with ./scripts/get-deps.sh. All *_SOURCE_DIR variables remain
# overridable for packagers who vendor the sources elsewhere.
get_filename_component(MLVM_DEFAULT_THIRD_PARTY_DIR
  "${CMAKE_CURRENT_LIST_DIR}/../third_party" ABSOLUTE)
set(MLVM_THIRD_PARTY_DIR "${MLVM_DEFAULT_THIRD_PARTY_DIR}"
  CACHE PATH "MLVM dependency source directory (see get-deps.sh)")
set(MLVM_SFSEXP_SOURCE_DIR "${MLVM_THIRD_PARTY_DIR}/sfsexp"
  CACHE PATH "sfsexp source directory (populated by get-deps.sh; never fetched at configure time)")

foreach(dependency IN ITEMS dparser metatk satie equinox-ng exolangtk
    lmdbxx kaguya lua klib tomlc99 isocline termlib)
  string(TOUPPER "${dependency}" key)
  string(REPLACE "-" "_" key "${key}")
  if(key STREQUAL "TOMLC99")
    set(key TOML)
  endif()
  set(MLVM_${key}_SOURCE_DIR "${MLVM_THIRD_PARTY_DIR}/${dependency}"
    CACHE PATH "${dependency} sources (see scripts/get-deps.sh)")
endforeach()
# Satie and VMWeave use the DomMEMTk API bundled with Satie. Upstream
# memtkx has since renamed that API; use the matching snapshot explicitly.
set(MLVM_MEMTKX_SOURCE_DIR "${MLVM_SATIE_SOURCE_DIR}/third_party/memtkx"
  CACHE PATH "Satie-compatible DomMEMTk headers")

set(MLVM_SFSEXP_SOURCES)
foreach(source cstring.c event_temp.c faststack.c io.c parser.c sexp.c
               sexp_memory.c sexp_ops.c sexp_vis.c)
  list(APPEND MLVM_SFSEXP_SOURCES "${MLVM_SFSEXP_SOURCE_DIR}/src/${source}")
endforeach()
set(MLVM_SFSEXP_AVAILABLE ON)
set(MLVM_SFSEXP_MISSING_FILE "")
foreach(file IN LISTS MLVM_SFSEXP_SOURCES ITEMS
    "${MLVM_SFSEXP_SOURCE_DIR}/src/sexp.h"
    "${MLVM_SFSEXP_SOURCE_DIR}/src/sexp_errors.h"
    "${MLVM_SFSEXP_SOURCE_DIR}/src/sexp_memory.h"
    "${MLVM_SFSEXP_SOURCE_DIR}/src/sexp_ops.h"
    "${MLVM_SFSEXP_SOURCE_DIR}/src/sexp_vis.h"
    "${MLVM_SFSEXP_SOURCE_DIR}/src/cstring.h"
    "${MLVM_SFSEXP_SOURCE_DIR}/src/faststack.h")
  if(NOT EXISTS "${file}")
    set(MLVM_SFSEXP_AVAILABLE OFF)
    set(MLVM_SFSEXP_MISSING_FILE "${file}")
    break()
  endif()
endforeach()
