cmake_minimum_required(VERSION 3.20)
project(Blackstone VERSION 0.1.0 LANGUAGES C)
include(CTest)
include(GNUInstallDirs)
find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
set(BLACKSTONE_DPARSER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/dparser" CACHE PATH "DParser sources")
set(D ${BLACKSTONE_DPARSER_DIR})
add_library(blackstone_dparse STATIC ${D}/arg.c ${D}/parse.c ${D}/scan.c
  ${D}/dsymtab.c ${D}/util.c ${D}/read_binary.c ${D}/dparse_tree.c ${D}/version.c)
target_include_directories(blackstone_dparse PUBLIC ${D})
target_compile_definitions(blackstone_dparse PRIVATE D_MAJOR_VERSION=1 D_MINOR_VERSION=30)
add_executable(blackstone_make_dparser ${D}/make_dparser.c ${D}/mkdparse.c
  ${D}/write_tables.c ${D}/grammar.g.c ${D}/gram.c ${D}/lex.c ${D}/lr.c)
target_link_libraries(blackstone_make_dparser PRIVATE blackstone_dparse)
set(TABLES ${CMAKE_CURRENT_BINARY_DIR}/blackstone.g.c)
add_custom_command(OUTPUT ${TABLES}
  COMMAND blackstone_make_dparser ${CMAKE_CURRENT_SOURCE_DIR}/grammar/blackstone.g -o ${TABLES}
  DEPENDS blackstone_make_dparser grammar/blackstone.g VERBATIM)
add_executable(blackstone-parser src/parser.c ${TABLES})
target_link_libraries(blackstone-parser PRIVATE blackstone_dparse)
target_compile_options(blackstone-parser PRIVATE -Wall -Wextra)
add_library(blackstone SHARED
  src/api.c src/profile.c src/bincode.c src/driver.c
  concern/module.c concern/loader.c concern/apply.c
  requires/module.c requires/loader.c requires/apply.c
  rewrite/pattern.c rewrite/match.c rewrite/mmap.c
  stdlib/concerns/HIR/Loops/Descriptor.c)
add_library(Blackstone::Runtime ALIAS blackstone)
target_compile_features(blackstone PUBLIC c_std_11)
target_include_directories(blackstone PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/blackstone>
  PRIVATE src profile)
target_compile_options(blackstone PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(blackstone PROPERTIES VERSION 0.1.0 SOVERSION 0)
add_executable(blackstone-native-example examples/native_profile.c)
target_link_libraries(blackstone-native-example PRIVATE Blackstone::Runtime)
target_compile_options(blackstone-native-example PRIVATE -Wall -Wextra -Wpedantic)
install(TARGETS blackstone blackstone-parser
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/blackstone)
if(BUILD_TESTING)
  add_executable(blackstone-native-test tests/native_runtime.c)
  target_link_libraries(blackstone-native-test PRIVATE Blackstone::Runtime)
  target_compile_options(blackstone-native-test PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME blackstone-native COMMAND blackstone-native-test)
  add_test(NAME blackstone-native-example COMMAND blackstone-native-example)
  add_test(NAME blackstone-examples COMMAND ${Python3_EXECUTABLE} -B examples/run_examples.py --engine both)
  add_test(NAME blackstone-contract COMMAND ${Python3_EXECUTABLE} -B -m unittest discover -s tests -v)
  set_tests_properties(blackstone-contract blackstone-examples PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    ENVIRONMENT "BLACKSTONE_PARSER=$<TARGET_FILE:blackstone-parser>;BLACKSTONE_LIBRARY=$<TARGET_FILE:blackstone>;PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}/python")
endif()
