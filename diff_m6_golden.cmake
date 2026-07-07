# diff_m6_golden.cmake
# Runs the M6 encounter golden dumper into a scratch file and STRICT byte-compares it against
# the committed golden vector. Drives the CTest gate `m6_encounter_goldenvec`.
#
# Same discipline as diff_golden.cmake (raw HEX read, no CRLF normalization), with one extra
# argument: the M6 dumper is data-driven and takes the EncounterWeights.csv path as argv[2],
# so the golden reflects the committed weights (single source of truth).
#
# Invocation (from CMakeLists.txt add_test):
#   cmake -DDUMPER=<m6_encounter_golden exe>
#         -DGOLDEN=<golden_m6_encounter_seed7.txt>
#         -DACTUAL=<scratch output in the build dir>
#         -DWEIGHTS=<EncounterWeights.csv>
#         -P diff_m6_golden.cmake

if(NOT DEFINED DUMPER)
  message(FATAL_ERROR "diff_m6_golden.cmake: -DDUMPER=<golden dumper exe> is required")
endif()
if(NOT DEFINED GOLDEN)
  message(FATAL_ERROR "diff_m6_golden.cmake: -DGOLDEN=<committed golden path> is required")
endif()
if(NOT EXISTS "${GOLDEN}")
  message(FATAL_ERROR "diff_m6_golden.cmake: committed golden not found: ${GOLDEN}")
endif()
if(NOT DEFINED WEIGHTS)
  message(FATAL_ERROR "diff_m6_golden.cmake: -DWEIGHTS=<EncounterWeights.csv> is required")
endif()
if(NOT EXISTS "${WEIGHTS}")
  message(FATAL_ERROR "diff_m6_golden.cmake: weights CSV not found: ${WEIGHTS}")
endif()
if(NOT DEFINED ACTUAL)
  set(ACTUAL "m6_encounter_golden.actual.txt")
endif()

# Delete any stale scratch output first so a dumper that fails to write cannot false-pass.
file(REMOVE "${ACTUAL}")

execute_process(
  COMMAND "${DUMPER}" "${ACTUAL}" "${WEIGHTS}"
  RESULT_VARIABLE dumper_rc
)
if(NOT dumper_rc EQUAL 0)
  message(FATAL_ERROR "diff_m6_golden.cmake: golden dumper failed (rc=${dumper_rc})")
endif()
if(NOT EXISTS "${ACTUAL}")
  message(FATAL_ERROR "diff_m6_golden.cmake: dumper produced no output at ${ACTUAL}")
endif()

# Raw-byte comparison (HEX read performs no text-mode / EOL translation).
file(READ "${GOLDEN}" golden_hex HEX)
file(READ "${ACTUAL}" actual_hex HEX)

if(NOT golden_hex STREQUAL actual_hex)
  message(FATAL_ERROR
    "diff_m6_golden.cmake: GOLDEN MISMATCH (byte-level)\n"
    "  committed: ${GOLDEN}\n"
    "  regenerated: ${ACTUAL}\n"
    "The dumper output differs from the committed golden. If the change is intentional "
    "(e.g. EncounterWeights.csv was edited), regenerate the golden and commit it; otherwise "
    "this is a determinism regression.")
endif()

message(STATUS "diff_m6_golden.cmake: golden byte-identical -> ${GOLDEN}")
