

ASSERT_DEFINED(PACKAGE_SOURCE_DIR CMAKE_CURRENT_SOURCE_DIR)

TRIBITS_INCLUDE_DIRECTORIES(${${PARENT_PACKAGE_NAME}_SOURCE_DIR}/stk_simd)
TRIBITS_INCLUDE_DIRECTORIES(${${PACKAGE_NAME}_SOURCE_DIR}/stk_simd)
TRIBITS_INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR})
TRIBITS_INCLUDE_DIRECTORIES(${${PACKAGE_NAME}_BINARY_DIR}/stk_simd)

FILE(GLOB SOURCES *.cpp)

TRIBITS_ADD_EXECUTABLE(
    simd_old_unit_tests
    SOURCES ${SOURCES}
    TESTONLYLIBS stk_unit_main
    COMM serial mpi
    NOEXEPREFIX
    )

TRIBITS_ADD_TEST(
   simd_old_unit_tests
   ARGS "--gtest_filter=StkSimd.SimdAddSubtractMultDivide"
   COMM serial mpi
   PASS_REGULAR_EXPRESSION "PASS"
   FAIL_REGULAR_EXPRESSION "FAIL"
   NUM_MPI_PROCS 1
   NOEXEPREFIX
   )
