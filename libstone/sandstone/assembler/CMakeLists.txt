# Sandstone assembler framework: library + `sandstone-as` CLI.
add_library(sandstone_assembler STATIC
  assembler.c
  parser.c
  preprocess.c
  directives.c
  binarizer.c
  lua.c
)
target_include_directories(sandstone_assembler
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/sandstone>
)
target_link_libraries(sandstone_assembler PUBLIC sandstone)
libstone_warnings(sandstone_assembler)
# dlopen() backend for the optional Lua hook; no link-time Lua needed.
target_link_libraries(sandstone_assembler PRIVATE ${CMAKE_DL_LIBS})

if(SANDSTONE_BUILD_TOOLS OR SANDSTONE_BUILD_TESTS)
  add_executable(sandstone-as driver.c)
  target_link_libraries(sandstone-as PRIVATE sandstone_assembler)
  libstone_warnings(sandstone-as)
endif()

if(SANDSTONE_BUILD_TOOLS)
  install(TARGETS sandstone-as RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
install(TARGETS sandstone_assembler
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(FILES assembler.h assembler.hpp
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/sandstone
)
