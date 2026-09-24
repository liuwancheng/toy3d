execute_process(
    COMMAND "${GENERATOR}" --input "${INPUT}" --header "${OUTPUT_HEADER}"
        --source "${OUTPUT_SOURCE}" --function register_invalid_types
    RESULT_VARIABLE result
    ERROR_VARIABLE error)

if(result EQUAL 0)
    message(FATAL_ERROR "invalid reflected field was accepted")
endif()
if(NOT error MATCHES "${EXPECTED}")
    message(FATAL_ERROR "error did not locate unsupported field: ${error}")
endif()
