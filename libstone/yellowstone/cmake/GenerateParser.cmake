execute_process(
  COMMAND "${PERL}" "${GENERATOR}" --entrypoint "${ENTRYPOINT}" "${GRAMMAR}"
  OUTPUT_FILE "${OUTPUT}" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  file(REMOVE "${OUTPUT}")
  message(FATAL_ERROR "Parser generation failed for ${GRAMMAR}: ${result}")
endif()
