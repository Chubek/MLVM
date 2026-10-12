# cephyr-libc — the freestanding C library Cephyr links programs against.
#
# The same sources serve two consumers:
#   * the host build below, which produces cephyr_libc for any target that
#     already has a start file and a way to make syscalls, and
#   * Cephyr itself, which compiles the same C11 subset and therefore builds
#     cephyr-libc with its own front end (see tests/bootstrap.py).

set(CEPHYR_LIBC_SOURCES
  src/string/mem.c
  src/string/str.c
  src/string/strtok.c
  src/string/strdup.c
  src/string/strcase.c
  src/string/strerror.c
  src/ctype/ctype.c
  src/errno/errno.c
  src/stdlib/abs.c
  src/stdlib/conv.c
  src/stdlib/intmax.c
  src/stdlib/rand.c
  src/stdlib/malloc.c
  src/stdlib/exit.c
  src/stdlib/qsort.c
  src/stdlib/bsearch.c
  src/stdlib/div.c
  src/stdlib/strtod.c
  src/stdio/file.c
  src/stdio/printf.c
  src/unistd/unistd.c
  src/time/time.c
  src/time/time_extra.c
  src/math/math.c
  src/setjmp/setjmp.c
  src/assert/assert.c
  src/start/libc_start.c)

add_library(cephyr_libc STATIC ${CEPHYR_LIBC_SOURCES})
add_library(MLVM::CephyrLibc ALIAS cephyr_libc)
target_include_directories(cephyr_libc PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_DATADIR}/mlvm/cephyr-libc/include>)
# Freestanding: no host headers, no builtins, no stack protector.
target_compile_options(cephyr_libc PRIVATE
  -ffreestanding -nostdinc -fno-builtin -fno-stack-protector -fno-pic -fno-pie
  -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-sanitize=all)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
  message(FATAL_ERROR "Bundled Cephyr runtime currently requires x86-64 Linux; select an external sysroot for other hosts")
endif()
target_compile_options(cephyr_libc PRIVATE -mcmodel=large)
set_target_properties(cephyr_libc PROPERTIES EXPORT_NAME CephyrLibc POSITION_INDEPENDENT_CODE OFF
  OUTPUT_NAME c ARCHIVE_OUTPUT_DIRECTORY "${CEPHYR_BUILD_SYSROOT}/lib")
file(MAKE_DIRECTORY "${CEPHYR_BUILD_SYSROOT}/include" "${CEPHYR_BUILD_SYSROOT}/lib")
file(COPY include/ DESTINATION "${CEPHYR_BUILD_SYSROOT}/include")
file(GLOB_RECURSE libc_headers CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/include/*")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${libc_headers})
add_custom_command(OUTPUT "${CEPHYR_BUILD_SYSROOT}/lib/crt0.o"
  COMMAND ${CMAKE_C_COMPILER} -c "${CMAKE_CURRENT_SOURCE_DIR}/src/crt/crt0_x86_64.S"
    -o "${CEPHYR_BUILD_SYSROOT}/lib/crt0.o"
  DEPENDS src/crt/crt0_x86_64.S VERBATIM)
add_custom_target(cephyr_crt0 ALL DEPENDS "${CEPHYR_BUILD_SYSROOT}/lib/crt0.o")
install(FILES "${CEPHYR_BUILD_SYSROOT}/lib/crt0.o"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/mlvm/cephyr-libc/lib")

if(MLVM_BUILD_TESTS)
  add_executable(cephyr_libc_selftest tests/selftest.c "${CEPHYR_BUILD_SYSROOT}/lib/crt0.o")
  add_dependencies(cephyr_libc_selftest cephyr_crt0)
  target_link_libraries(cephyr_libc_selftest PRIVATE cephyr_libc)
  target_compile_options(cephyr_libc_selftest PRIVATE
    -ffreestanding -nostdinc -fno-builtin -fno-stack-protector -fno-pie -fno-sanitize=all)
  target_link_options(cephyr_libc_selftest PRIVATE -nostdlib -static -no-pie -fno-sanitize=all)
  add_test(NAME cephyr-libc-host COMMAND cephyr_libc_selftest)
endif()

if(MLVM_BUILD_TESTS AND MLVM_BUILD_TOOLS AND Python3_Interpreter_FOUND)
  add_test(NAME cephyr-libc-bootstrap COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/bootstrap.py
    $<TARGET_FILE:cephyr> ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME cephyr-libc-runtime COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime.py $<TARGET_FILE:cephyr>)
endif()
