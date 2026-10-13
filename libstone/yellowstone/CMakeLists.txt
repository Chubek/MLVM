cmake_minimum_required(VERSION 3.20)
project(Yellowstone VERSION 0.1.0 LANGUAGES C CXX)
include(../cmake/LibstoneBuild.cmake)
option(YELLOWSTONE_BUILD_TOOLS "Build Yellowstone command-line tools" "${LIBSTONE_BUILD_TOOLS}")
option(YELLOWSTONE_BUILD_TESTS "Build Yellowstone tests" "${LIBSTONE_BUILD_TESTS}")
if(YELLOWSTONE_BUILD_TESTS)
  enable_testing()
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(QobjDSL)

option(QOBJFILE_WERROR "Treat warnings as errors" OFF)
option(QOBJFILE_DOMTERM "Build DomTERM terminal and Termscript support" ON)

if(QOBJFILE_DOMTERM)
  add_subdirectory(external/domterm ${CMAKE_CURRENT_BINARY_DIR}/domterm)
endif()

# The inspection tools share argument parsing, formatting and archive-aware
# input, so that lives in one interface target rather than a header each tool
# includes by relative path.
add_library(qobjcommon INTERFACE)
add_library(qobjfile::qobjcommon ALIAS qobjcommon)
# Backwards-compatible alias: older tool sources included
# "../qobjcommon/..."; the directory is now common/.
add_library(qobjfile_qobjcommon ALIAS qobjcommon)
target_compile_features(qobjcommon INTERFACE cxx_std_20)
target_link_libraries(qobjcommon INTERFACE qbfd::qbfd)
if(QOBJFILE_DOMTERM)
  target_link_libraries(qobjcommon INTERFACE domterm termscript_standalone)
  target_include_directories(qobjcommon INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/external/domterm)
endif()
target_include_directories(qobjcommon INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/common
  ${CMAKE_CURRENT_SOURCE_DIR}/qbfd
)

function(qobjfile_tool tool dir)
  add_executable(${tool} ${dir}/main.cpp)
  target_link_libraries(${tool} PRIVATE qobjcommon qbfd::qbfd)
  target_compile_options(${tool} PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic>)
  if(QOBJFILE_WERROR)
    target_compile_options(${tool} PRIVATE
      $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Werror>)
  endif()
  target_include_directories(${tool} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
endfunction()

# The header-only reader library lives in qbfd/ (target qbfd::qbfd).
add_subdirectory(qbfd)

# Directory names differ from tool names (dump -> qobjdump, ...), so the
# mapping is explicit rather than assumed.
if(YELLOWSTONE_BUILD_TOOLS)
qobjfile_tool(qobjdump dump)
qobjfile_tool(qobjsize size)
qobjfile_tool(qobjcp copy)
qobjfile_tool(qobjstr string)
qobjfile_tool(qobjstrip strip)
qobjfile_tool(qobjnm nm)
qobjfile_tool(qobjar ar)

# Format specific inspection front ends share qbfd's canonical object model.
foreach(reader readelf readexe readmacho)
  add_executable(${reader} read/main.cpp)
  target_link_libraries(${reader} PRIVATE qobjcommon qbfd::qbfd)
  target_include_directories(${reader} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
endforeach()
endif()

add_library(binscript STATIC binscript/binscript.c)
target_include_directories(binscript PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/binscript)
if(YELLOWSTONE_BUILD_TOOLS)
add_executable(binscript_cli binscript/main.c)
target_link_libraries(binscript_cli PRIVATE binscript)
set_target_properties(binscript_cli PROPERTIES OUTPUT_NAME binscript)
endif()
find_program(YELLOWSTONE_PERL perl)
if(YELLOWSTONE_PERL)
  add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/binscript_parser.c
    COMMAND ${CMAKE_COMMAND} -DPERL=${YELLOWSTONE_PERL}
            -DGENERATOR=${CMAKE_CURRENT_SOURCE_DIR}/scripts/aurocks.pl
            -DENTRYPOINT=bs_parse -DGRAMMAR=${CMAKE_CURRENT_SOURCE_DIR}/binscript/binscript.g
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/binscript_parser.c
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateParser.cmake
    DEPENDS scripts/aurocks.pl binscript/binscript.g cmake/GenerateParser.cmake VERBATIM)
  add_custom_target(binscript_parser DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/binscript_parser.c)
  add_dependencies(binscript binscript_parser)
endif()

# The Unix-style linker (qobjld library + CLI) in ld/.
add_subdirectory(ld)

# The command-line tools are only meaningful once the library's own tests pass,
# so they are registered as tests too rather than merely built.
if(YELLOWSTONE_BUILD_TESTS AND YELLOWSTONE_BUILD_TOOLS)
  add_test(NAME qobjfile_tools_help
           COMMAND qobjdump --help)
  add_test(NAME qobjstr_help COMMAND qobjstr --help)
  add_test(NAME qobjnm_help COMMAND qobjnm --help)
  add_test(NAME qobjsize_help COMMAND qobjsize --help)
  add_test(NAME qobjcp_help COMMAND qobjcp --help)
  add_test(NAME qobjar_help COMMAND qobjar --help)
  add_test(NAME qobjstrip_help COMMAND qobjstrip --help)
  add_test(NAME qobjld_help COMMAND qobjld --help)
  add_test(NAME qobjfile_cli
           COMMAND ${CMAKE_COMMAND}
                   -DTOOLS_DIR=$<TARGET_FILE_DIR:qobjdump>
                   -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run-cli-tests.cmake)
  add_test(NAME qobjld_cli
           COMMAND ${CMAKE_COMMAND}
                   -DTOOLS_DIR=$<TARGET_FILE_DIR:qobjld>
                   -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run-ld-tests.cmake)
endif()

if(YELLOWSTONE_BUILD_TOOLS)
  install(TARGETS qobjdump qobjsize qobjcp qobjstr qobjstrip qobjnm qobjar qobjld readelf readexe readmacho binscript_cli
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
install(TARGETS qld binscript
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY qbfd/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/qbfd
        FILES_MATCHING PATTERN "*.hpp")
install(FILES common/qdsl.hpp common/isa.hpp "${MLVM_METATK_SOURCE_DIR}/DSLtk/DSLtk.hpp"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/qobjfile)
# qBFDCore.hpp retains its source-compatible ../common/qdsl.hpp include.
install(FILES common/qdsl.hpp "${MLVM_METATK_SOURCE_DIR}/DSLtk/DSLtk.hpp"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/common)
install(FILES ld/ld.h ld/ld.hpp
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/qobjld)
