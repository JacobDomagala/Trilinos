


#
# A) Package-specific configuration options
#

TRIBITS_CONFIGURE_FILE(${PACKAGE_NAME}_config.h)


#SET(CURRENT_PACKAGE ShyLU)
CONFIGURE_FILE("${Trilinos_SOURCE_DIR}/packages/Trilinos_DLLExportMacro.h.in"
  ${CMAKE_CURRENT_BINARY_DIR}/${PACKAGE_NAME}_DLLExportMacro.h)


#
# B) Define the header and source files (and include directories)
#

SET(HEADERS "")
SET(SOURCES "")


# Set the base Trilinos directory in order to find Trilinos_version.h

ASSERT_DEFINED(PROJECT_SOURCE_DIR)
ASSERT_DEFINED(PROJECT_BINARY_DIR)

TRIBITS_INCLUDE_DIRECTORIES(${PROJECT_SOURCE_DIR})

#
# Include packages/src directory
#
TRIBITS_INCLUDE_DIRECTORIES(${CMAKE_CURRENT_BINARY_DIR})
TRIBITS_INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR})
TRIBITS_INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/epetra)
TRIBITS_INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/interfaces)


APPEND_SET(HEADERS
  ${CMAKE_CURRENT_BINARY_DIR}/${PACKAGE_NAME}_config.h
  )

TRIBITS_INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR})

APPEND_SET(HEADERS
  epetra/shylu_internal_gmres.h
  epetra/shylu_internal_gmres_tools.h
  epetra/IQRSolver.h
  epetra/shylu_debug_manager.hpp
  epetra/Ifpack_ShyLU.h
  epetra/shylu_amesos_schur_operator.h
  epetra/shylu_util.h
  epetra/shylu_symbolic.h
  epetra/shylu.h
  epetra/shylu_probing_operator.h
  epetra/shylu_local_schur_operator.h
  epetra/shylu_internal.h
  epetra/shylu_config.h
  epetra/shylu_partition_interface.hpp
  epetra/shylu_partition_interface_def.hpp
  epetra/shylu_partition_interface_decl.hpp
  epetra/shylu_directsolver_interface.hpp
  epetra/shylu_directsolver_interface_def.hpp
  epetra/shylu_directsolver_interface_decl.hpp
  )

# Set sources

APPEND_SET(SOURCES
  epetra/IQRSolver.cpp
  epetra/shylu_factor.cpp
  epetra/Ifpack_ShyLU.cpp
  epetra/shylu_amesos_schur_operator.cpp
  epetra/shylu_probing_operator.cpp
  epetra/shylu_local_schur_operator.cpp
  epetra/shylu_util.cpp
  epetra/shylu_solve.cpp
  epetra/shylu_schur.cpp
  epetra/shylu_partition_interface.cpp
  epetra/shylu_directsolver_interface.cpp
  )

TRIBITS_ADD_LIBRARY(
  shylu_ddcore
  HEADERS ${HEADERS}
  SOURCES ${SOURCES}
  )
