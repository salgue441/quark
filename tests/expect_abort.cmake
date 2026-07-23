# Expect the child process to abort (non-zero / signal) and print the
# HazardDomain live-handle fatal diagnostic.
#
# CTest's WILL_FAIL does not invert "Subprocess aborted" (Exception), so
# we wrap the binary in this script and treat a successful abort as PASS.

if(NOT DEFINED TEST_COMMAND)
  message(FATAL_ERROR "TEST_COMMAND must be set to the abort test binary")
endif()

execute_process(
  COMMAND "${TEST_COMMAND}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err
)

set(combined "${out}\n${err}")

if(result EQUAL 0)
  message(FATAL_ERROR
    "expected abort, but process exited 0\n${combined}")
endif()

if(NOT combined MATCHES "HazardDomain destroyed with live ThreadHazardHandle")
  message(FATAL_ERROR
    "expected fatal diagnostic missing (result=${result})\n${combined}")
endif()
