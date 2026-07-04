# diff_golden.cmake
# Runs the M5 golden dumper into a scratch file and STRICT byte-compares it against the
# committed golden vector. Drives the CTest gate `m5_loadout_goldenvec`.
#
# Invocation (from CMakeLists.txt add_test):
#   cmake -DDUMPER=<path to m5_loadout_golden exe>
#         -DGOLDEN=<path to golden_m5_loadout_seed7.txt>
#         -DACTUAL=<scratch output path in the build dir>
#         -P diff_golden.cmake
#
# The compare is STRICT (raw bytes via file(READ ... HEX)) with NO CRLF normalization, so an
# end-of-line regression is CAUGHT, not masked. The dumper writes bare LF via a binary
# std::ofstream and .gitattributes keeps the committed golden as LF on every platform, so the
# strict compare succeeds on Windows (MSVC), WSL/Linux (g++), and CI alike.

if(NOT DEFINED DUMPER)
  message(FATAL_ERROR "diff_golden.cmake: -DDUMPER=<golden dumper exe> is required")
endif()
if(NOT DEFINED GOLDEN)
  message(FATAL_ERROR "diff_golden.cmake: -DGOLDEN=<committed golden path> is required")
endif()
if(NOT EXISTS "${GOLDEN}")
  message(FATAL_ERROR "diff_golden.cmake: committed golden not found: ${GOLDEN}")
endif()
if(NOT DEFINED ACTUAL)
  set(ACTUAL "m5_loadout_golden.actual.txt")   # falls back to the test's working directory
endif()

# Delete any stale scratch output first: a dumper that fails to write must not be able to
# false-pass against a leftover file from a previous run.
file(REMOVE "${ACTUAL}")

execute_process(
  COMMAND "${DUMPER}" "${ACTUAL}"
  RESULT_VARIABLE dumper_rc
)
if(NOT dumper_rc EQUAL 0)
  message(FATAL_ERROR "diff_golden.cmake: golden dumper failed (rc=${dumper_rc})")
endif()
if(NOT EXISTS "${ACTUAL}")
  message(FATAL_ERROR "diff_golden.cmake: dumper produced no output at ${ACTUAL}")
endif()

# Raw-byte comparison (HEX read performs no text-mode / EOL translation).
file(READ "${GOLDEN}" golden_hex HEX)
file(READ "${ACTUAL}" actual_hex HEX)

if(NOT golden_hex STREQUAL actual_hex)
  message(FATAL_ERROR
    "diff_golden.cmake: GOLDEN MISMATCH (byte-level)\n"
    "  committed: ${GOLDEN}\n"
    "  regenerated: ${ACTUAL}\n"
    "The dumper output differs from the committed golden. If the change is intentional, "
    "regenerate the golden with the dumper and commit it; otherwise this is a determinism regression.")
endif()

message(STATUS "diff_golden.cmake: golden byte-identical -> ${GOLDEN}")
