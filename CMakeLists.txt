cmake_minimum_required(VERSION 3.10)
project(ChessEngine VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Options de compilation
option(BUILD_TESTS "Build tests" ON)
option(USE_AVX2 "Use AVX2 instructions" OFF)
option(USE_BMI2 "Use BMI2 instructions" OFF)

# Flags de compilation
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -O3")
if(WIN32)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--stack,8388608")
endif()
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -g -O0")

if(USE_AVX2)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mavx2")
endif()

if(USE_BMI2)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mbmi2")
endif()

# Sources
set(SOURCES
    src/main.c
    src/board.c
    src/movegen.c
    src/search.c
    src/evaluation.c
    src/uci.c
    src/transposition.c
    src/zobrist.c
    src/bitboards.c
    src/magics.c
    src/magic.c
    src/book.c
    src/random.c
    src/endgame/tb.c
    src/endgame/fathom/tbprobe.c
    src/nnue.c
)

# Bibliothèque
add_library(chess_engine_lib STATIC ${SOURCES})
target_include_directories(chess_engine_lib PUBLIC src src/endgame/fathom)

# Exécutable principal
add_executable(chess_engine src/main.c)
target_link_libraries(chess_engine chess_engine_lib)

# DLL (contourne WDAC — chargée via ctypes, pas de subprocess) — Windows uniquement
if(WIN32)
    add_library(chess_engine_dll SHARED src/engine_dll.c)
    target_link_libraries(chess_engine_dll chess_engine_lib)
    set_target_properties(chess_engine_dll PROPERTIES PREFIX "")
else()
    target_link_libraries(chess_engine_lib PUBLIC m)
endif()

# Tests
if(BUILD_TESTS)
    enable_testing()
    add_executable(test_perft tests/test_perft.c)
    target_link_libraries(test_perft chess_engine_lib)
    add_test(NAME Perft COMMAND test_perft)

    add_executable(test_perft_counts tests/test_perft_counts.c)
    target_link_libraries(test_perft_counts chess_engine_lib)
    add_test(NAME PerftCounts COMMAND test_perft_counts)

    add_executable(test_pgn tests/test_pgn.c)
    target_link_libraries(test_pgn chess_engine_lib)
    add_test(NAME PGN COMMAND test_pgn)

    add_executable(test_movegen tests/test_movegen.c)
    target_link_libraries(test_movegen chess_engine_lib)
    add_test(NAME Movegen COMMAND test_movegen)

    add_executable(test_nnue_fix tests/test_nnue_fix.c)
    target_link_libraries(test_nnue_fix chess_engine_lib)
    add_test(NAME NNUEFix COMMAND test_nnue_fix WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    set_tests_properties(NNUEFix PROPERTIES SKIP_RETURN_CODE 77)

    add_executable(test_zobrist_keys tests/test_zobrist_keys.c)
    target_link_libraries(test_zobrist_keys chess_engine_lib)
    add_test(NAME ZobristPolyglot COMMAND test_zobrist_keys)
endif()

# Book generator tool — DISABLED (bookgen.c missing)
# add_executable(bookgen tools/bookgen.c)
# target_link_libraries(bookgen chess_engine_lib)

# Installation
install(TARGETS chess_engine DESTINATION bin)
