# Narrow integration targets keep vendor implementation types private.
libstone_require_files(MLVM_SATIE_SOURCE_DIR include/Satie.hpp src/SatieMemory.cpp)
libstone_require_files(MLVM_MEMTKX_SOURCE_DIR include/dommemtk/space/free_list.hpp)
libstone_require_files(MLVM_EQUINOX_NG_SOURCE_DIR EquinoxNG.hpp)
libstone_require_files(MLVM_METATK_SOURCE_DIR SExprTk/include/SExprTk.hpp
  DSLtk/DSLtk.hpp EkippX/EkippX.hpp third_party/SerdeTk/include/SerdeTk.hpp)
libstone_require_files(MLVM_DPARSER_SOURCE_DIR dparse.h parse.c grammar.g.c make_dparser.c)
libstone_require_files(MLVM_EXOLANGTK_SOURCE_DIR CMakeLists.txt)
find_package(Threads REQUIRED)
add_library(limestone_satie INTERFACE)
add_library(satie::satie ALIAS limestone_satie)
target_include_directories(limestone_satie SYSTEM INTERFACE
  ${MLVM_SATIE_SOURCE_DIR}/include)
# New Satie headers (CDCL clause database, theory encoders) allocate transient
# scratch through satie::MemoryResource, which lives in Satie's compiled
# sources and is backed by the vendored memtkx headers. Compile just that
# translation unit here; the libglr-backed frontend parsers stay out so
# consumers do not inherit the parser backends.
add_library(limestone_satie_runtime STATIC
  ${MLVM_SATIE_SOURCE_DIR}/src/SatieMemory.cpp)
target_include_directories(limestone_satie_runtime SYSTEM PUBLIC
  $<BUILD_INTERFACE:${MLVM_SATIE_SOURCE_DIR}/include>)
target_include_directories(limestone_satie_runtime SYSTEM PRIVATE
  ${MLVM_MEMTKX_SOURCE_DIR}/include)
target_compile_features(limestone_satie_runtime PUBLIC cxx_std_20)
target_link_libraries(limestone_satie_runtime PRIVATE Threads::Threads)
target_link_libraries(limestone_satie INTERFACE limestone_satie_runtime)

add_library(limestone_equinox INTERFACE)
add_library(equinox::equinox-ng ALIAS limestone_equinox)
target_include_directories(limestone_equinox SYSTEM INTERFACE
  ${MLVM_EQUINOX_NG_SOURCE_DIR})
target_compile_definitions(limestone_equinox INTERFACE EQUINOXNG_DISABLE_DSLUTILS)

foreach(component IN ITEMS sexprtk dsltk ekippx)
  add_library(limestone_${component} INTERFACE)
  add_library(metatk::${component} ALIAS limestone_${component})
endforeach()
target_include_directories(limestone_sexprtk SYSTEM INTERFACE
  ${MLVM_METATK_SOURCE_DIR}/SExprTk/include)
target_include_directories(limestone_dsltk SYSTEM INTERFACE
  ${MLVM_METATK_SOURCE_DIR}/DSLtk)
target_include_directories(limestone_ekippx SYSTEM INTERFACE
  ${MLVM_METATK_SOURCE_DIR}/EkippX
  ${MLVM_METATK_SOURCE_DIR}
  ${MLVM_METATK_SOURCE_DIR}/DSLtk
  ${MLVM_METATK_SOURCE_DIR}/third_party/SerdeTk/include)

# Build the supplied GLR generator and runtime without its sample programs.
set(dparser_dir "${MLVM_DPARSER_SOURCE_DIR}")
add_library(limestone_dparse STATIC
  ${dparser_dir}/arg.c ${dparser_dir}/parse.c ${dparser_dir}/scan.c
  ${dparser_dir}/dsymtab.c ${dparser_dir}/util.c ${dparser_dir}/read_binary.c
  ${dparser_dir}/dparse_tree.c ${dparser_dir}/version.c)
add_library(limestone_mkdparse STATIC
  ${dparser_dir}/mkdparse.c ${dparser_dir}/write_tables.c
  ${dparser_dir}/grammar.g.c ${dparser_dir}/gram.c ${dparser_dir}/lex.c
  ${dparser_dir}/lr.c)
foreach(target IN ITEMS limestone_dparse limestone_mkdparse)
  target_include_directories(${target} SYSTEM PUBLIC $<BUILD_INTERFACE:${dparser_dir}>)
  target_compile_definitions(${target} PRIVATE D_MAJOR_VERSION=1 D_MINOR_VERSION=30)
endforeach()
add_executable(limestone-make-dparser ${dparser_dir}/make_dparser.c)
target_link_libraries(limestone-make-dparser PRIVATE limestone_mkdparse limestone_dparse)
find_package(Perl REQUIRED)
function(limestone_grammar name)
  string(REPLACE "-" "_" identifier ${name})
  set(directory ${PROJECT_BINARY_DIR}/generated/parsers)
  set(output ${directory}/${name}.c)
  set(header ${directory}/${name}_ast.hpp)
  set(source ${directory}/${name}_ast.cpp)
  add_custom_command(OUTPUT ${output}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${directory}
    COMMAND limestone-make-dparser ${PROJECT_SOURCE_DIR}/parsers/${name}.g
      -o ${output} -i ${identifier}
    DEPENDS limestone-make-dparser ${PROJECT_SOURCE_DIR}/parsers/${name}.g
    VERBATIM)
  add_custom_command(OUTPUT ${header} ${source}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${directory}
    COMMAND ${PERL_EXECUTABLE} ${PROJECT_SOURCE_DIR}/scripts/syngen.pl
      --grammar ${PROJECT_SOURCE_DIR}/parsers/${name}.g
      --header ${header} --source ${source} ${PROJECT_SOURCE_DIR}/parsers/${name}.absyn
    COMMAND ${CMAKE_COMMAND} -E touch ${header} ${source}
    DEPENDS ${PROJECT_SOURCE_DIR}/scripts/syngen.pl
      ${PROJECT_SOURCE_DIR}/parsers/${name}.g ${PROJECT_SOURCE_DIR}/parsers/${name}.absyn
    VERBATIM)
  set(limestone_parser_sources ${limestone_parser_sources} ${output} PARENT_SCOPE)
  set(limestone_ast_sources ${limestone_ast_sources} ${source} PARENT_SCOPE)
  set(limestone_ast_headers ${limestone_ast_headers} ${header} PARENT_SCOPE)
endfunction()
foreach(grammar IN ITEMS bin2bin isa limeburg limestone machine-ir regtl schedrow traceml tuner unisel)
  limestone_grammar(${grammar})
endforeach()
add_custom_target(limestone-generate-parsers
  DEPENDS ${limestone_parser_sources} ${limestone_ast_sources} ${limestone_ast_headers})
add_library(limestone_parser_tables STATIC ${limestone_parser_sources})
add_dependencies(limestone_parser_tables limestone-generate-parsers)
target_link_libraries(limestone_parser_tables PRIVATE limestone_dparse)

if(NOT TARGET ExolangTk::ExtensionTk)
  add_subdirectory("${MLVM_EXOLANGTK_SOURCE_DIR}"
    "${PROJECT_BINARY_DIR}/third_party/exolangtk" EXCLUDE_FROM_ALL)
endif()

option(LIMESTONE_ENABLE_PERSISTENT_CACHE "Enable Bin2Bin's LMDB persistent cache" ON)
if(LIMESTONE_ENABLE_PERSISTENT_CACHE)
  libstone_require_files(MLVM_LMDBXX_SOURCE_DIR lmdb++.h)
  find_path(LMDB_INCLUDE_DIR lmdb.h REQUIRED)
  find_library(LMDB_LIBRARY NAMES lmdb REQUIRED)
  add_library(limestone_lmdb INTERFACE)
  target_include_directories(limestone_lmdb SYSTEM INTERFACE
    ${MLVM_LMDBXX_SOURCE_DIR} ${LMDB_INCLUDE_DIR})
  target_link_libraries(limestone_lmdb INTERFACE ${LMDB_LIBRARY})
  target_compile_definitions(limestone_lmdb INTERFACE LIMESTONE_HAS_LMDB)
endif()
