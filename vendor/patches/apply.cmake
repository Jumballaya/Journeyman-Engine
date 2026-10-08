# Applies PATCH to the current directory (a fetched dependency's checkout)
# unless it already is: re-configuring a build must not fail on it.
#   cmake -DPATCH=<file> -P apply.cmake
execute_process(COMMAND git apply --reverse --check ${PATCH} RESULT_VARIABLE already OUTPUT_QUIET ERROR_QUIET)
if(already EQUAL 0)
  return()
endif()
execute_process(COMMAND git apply ${PATCH} RESULT_VARIABLE failed)
if(failed)
  message(FATAL_ERROR "can't apply ${PATCH}")
endif()
