// @HEADER
// ************************************************************************
//
//                           Intrepid Package
//                 Copyright (2007) Sandia Corporation
//
// Under terms of Contract DE-AC04-94AL85000, there is a non-exclusive
// license for use of this work by or on behalf of the U.S. Government.
//
// This library is free software; you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as
// published by the Free Software Foundation; either version 2.1 of the
// License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301
// USA
// Questions? Contact Pavel Bochev  (pbboche@sandia.gov),
//                    Denis Ridzal  (dridzal@sandia.gov),
//                    Kara Peterson (kjpeter@sandia.gov).
//
// ************************************************************************
// @HEADER

/** \file   example_Maxwell.cpp
    \brief  Example solution of the eddy current Maxwell's equations using
    curl-conforming (edge) elements.


    This example uses the following Trilinos packages:
    \li        Pamgen to generate a Hexahedral mesh.
    \li        Intrepid to build the discretization matrices and right-hand side.
    \li        Epetra to handle the global matrix and vector.
    \li        ML to solve the linear system.



    \verbatim

    Maxwell System:

    curl x mu^{-1} curl E + sigma E = f

    Corresponding discrete linear system for edge element coeficients (x):

    (Kc + Mc)x = b

    Kc    - Hcurl stiffness matrix
    Mc    - Hcurl mass matrix
    b     - right hand side vector

    \endverbatim

    \author Created by P. Bochev, D. Ridzal, K. Peterson, D. Hensinger, C. Siefert.

    \remark Usage
    \verbatim

    ./TrilinosCouplings_examples_scaling_Example_Maxwell.exe  inputfile.xml


    inputfile.xml (optional)  -  xml input file containing Pamgen mesh description
    and material parameters for each Pamgen block,
    if not present code attempts to read Maxwell.xml.

    \endverbatim

    Input files available in Trilinos for use with the Maxwell driver:

    \li Maxwell.xml - basic input file with box mesh and one mesh block
    \li Ninja.xml - input file with distorted mesh (shaped like Ninja star) and one mesh block
    \li Maxwell_in_block.xml - input file with box mesh with a center block with different material values.

**/

/**************************************************************/
/*                          Includes                          */
/**************************************************************/
// TrilinosCouplings includes
#include "TrilinosCouplings_config.h"
#include "TrilinosCouplings_Pamgen_Utils.hpp"
#include "TrilinosCouplings_Statistics.hpp"

// Intrepid includes
#include "Intrepid_FunctionSpaceTools.hpp"
#include "Intrepid_FieldContainer.hpp"
#include "Intrepid_CellTools.hpp"
#include "Intrepid_ArrayTools.hpp"
#include "Intrepid_HCURL_HEX_I1_FEM.hpp"
#include "Intrepid_HGRAD_HEX_C1_FEM.hpp"
#include "Intrepid_RealSpaceTools.hpp"
#include "Intrepid_DefaultCubatureFactory.hpp"
#include "Intrepid_Utils.hpp"

// Epetra includes
#include "Epetra_Time.h"
#include "Epetra_Map.h"
#ifdef HAVE_MPI
#include "Epetra_MpiComm.h"
#else
#include "Epetra_SerialComm.h"
#endif
#include "Epetra_Import.h"
#include "Epetra_Export.h"
#include "Epetra_CrsMatrix.h"
#include "Epetra_FECrsMatrix.h"
#include "Epetra_FEVector.h"
#include "Epetra_LinearProblem.h"

// Teuchos includes
#include "Teuchos_oblackholestream.hpp"
#include "Teuchos_RCP.hpp"
#include "Teuchos_BLAS.hpp"
#include "Teuchos_GlobalMPISession.hpp"
#include "Teuchos_ParameterList.hpp"
#include "Teuchos_XMLParameterListHelpers.hpp"
#include "Teuchos_Comm.hpp"
#include "Teuchos_DefaultComm.hpp"

// Shards includes
#include "Shards_CellTopology.hpp"

// EpetraExt includes
#include "EpetraExt_RowMatrixOut.h"
#include "EpetraExt_MultiVectorOut.h"
#include "EpetraExt_VectorOut.h"
#include "EpetraExt_MatrixMatrix.h"

// AztecOO includes
#include "AztecOO.h"

// ML Includes
#include "ml_MultiLevelPreconditioner.h"
#include "ml_RefMaxwell_11_Operator.h"
#include "ml_RefMaxwell.h"
#include "ml_EdgeMatrixFreePreconditioner.h"
#include "ml_epetra_utils.h"

#ifdef HAVE_TRILINOSCOUPLINGS_MUELU
// Xpetra
#include <Xpetra_MultiVector.hpp>
#include <Xpetra_MultiVectorFactory.hpp>
#include <Xpetra_CrsMatrix.hpp>
#include <Xpetra_CrsMatrixWrap.hpp>
#include <Xpetra_Matrix.hpp>

#ifdef HAVE_TRILINOSCOUPLINGS_AVATAR
#  include "MueLu_AvatarInterface.hpp"
#endif //HAVE_TRILINOSCOUPLINGS_AVATAR

// MueLu
#include <MueLu_RefMaxwell.hpp>
#ifdef HAVE_MUELU_EPETRA
#include <MueLu_AztecEpetraOperator.hpp>
#endif
#include <MueLu_Exceptions.hpp>

#endif

// Pamgen includes
#include "create_inline_mesh.h"
#include "pamgen_im_exodusII_l.h"
#include "pamgen_im_ne_nemesisI_l.h"
#include "pamgen_extras.h"


#ifdef HAVE_TRILINOSCOUPLINGS_STRATIMIKOS
#include "Stratimikos_DefaultLinearSolverBuilder.hpp"
#ifdef HAVE_TRILINOSCOUPLINGS_MUELU
#include <Stratimikos_MueLuHelpers.hpp>
#endif
#endif

#include "TrilinosCouplings_IntrepidPoissonExampleHelpers.hpp"


#define ABS(x) ((x)>0?(x):-(x))
#define SQR(x) ((x)*(x))


/*** Uncomment if you would like output data for plotting ***/

using namespace std;
using namespace Intrepid;


/*********************************************************/
/*                     Typedefs                          */
/*********************************************************/
typedef Intrepid::FunctionSpaceTools     IntrepidFSTools;
typedef Intrepid::RealSpaceTools<double> IntrepidRSTools;
typedef Intrepid::CellTools<double>      IntrepidCTools;


struct fecomp{
  bool operator () ( topo_entity* x,  topo_entity*  y )const
  {
    if(x->sorted_local_node_ids < y->sorted_local_node_ids)return true;
    return false;
  }
};

template<class Container>
double distance(Container &nodeCoord, int i1, int i2) {
  double dist = 0.0;
  for(int j=0; j<3; j++) 
    dist+= SQR( nodeCoord(i1,j) - nodeCoord(i2,j) );
  return sqrt(dist);
}

/**********************************************************************************/
/***************** FUNCTION DECLARATION FOR ML PRECONDITIONER *********************/
/**********************************************************************************/

/** \brief  ML Preconditioner

    \param  ProblemType        [in]    problem type
    \param  MLList             [in]    ML parameter list
    \param  CurlCurl           [in]    H(curl) stiffness matrix
    \param  D0clean            [in]    Edge to node stiffness matrix
    \param  M0inv              [in]    H(grad) mass matrix inverse
    \param  Ms                 [in]    H(curl) mass matrix w/ sigma
    \param  M1                 [in]    H(curl) mass matrix w/o sigma
    \param  xh                 [out]   solution vector
    \param  b                  [in]    right-hand-side vector
    \param  TotalErrorResidual [out]   error residual
    \param  TotalErrorExactSol [out]   error in xh

*/
void TestMultiLevelPreconditioner_Maxwell(char ProblemType[],
                                          Teuchos::ParameterList   & MLList,
                                          Epetra_CrsMatrix   & CurlCurl,
                                          Epetra_CrsMatrix   & D0clean,
                                          Epetra_CrsMatrix   & M0inv,
                                          Epetra_CrsMatrix   & Ms,
                                          Epetra_CrsMatrix   & M1,
                                          Epetra_MultiVector & xh,
                                          Epetra_MultiVector & b,
                                          double & TotalErrorResidual,
                                          double & TotalErrorExactSol);

#if defined(HAVE_MUELU_EPETRA) and defined(HAVE_TRILINOSCOUPLINGS_MUELU)
/** \brief  MueLu Preconditioner

    \param  ProblemType        [in]    problem type
    \param  MLList             [in]    ML parameter list
    \param  CurlCurl           [in]    H(curl) stiffness matrix
    \param  D0clean            [in]    Edge to node stiffness matrix
    \param  M0inv              [in]    H(grad) mass matrix inverse
    \param  Ms                 [in]    H(curl) mass matrix w/ sigma
    \param  M1                 [in]    H(curl) mass matrix w/o sigma
    \param  xh                 [out]   solution vector
    \param  b                  [in]    right-hand-side vector
    \param  TotalErrorResidual [out]   error residual
    \param  TotalErrorExactSol [out]   error in xh

*/
void TestMueLuMultiLevelPreconditioner_Maxwell(char ProblemType[],
                                               Teuchos::ParameterList   & MLList,
                                               Epetra_CrsMatrix   & CurlCurl,
                                               Epetra_CrsMatrix   & D0clean,
                                               Epetra_CrsMatrix   & M0inv,
                                               Epetra_CrsMatrix   & Ms,
                                               Epetra_CrsMatrix   & M1,
                                               Epetra_MultiVector & coords,
                                               Epetra_MultiVector & xh,
                                               Epetra_MultiVector & b,
                                               double & TotalErrorResidual,
                                               double & TotalErrorExactSol);
#endif


/** \brief  ML Preconditioner

    \param  ProblemType        [in]    problem type
    \param  MLList             [in]    ML parameter list
    \param  CurlCurl           [in]    H(curl) stiffness matrix
    \param  D0clean            [in]    Edge to node stiffness matrix
    \param  M0inv              [in]    H(grad) mass matrix inverse
    \param  Ms                 [in]    H(curl) mass matrix w/ sigma
    \param  M1                 [in]    H(curl) mass matrix w/o sigma
    \param  xh                 [out]   solution vector
    \param  b                  [in]    right-hand-side vector
    \param  TotalErrorResidual [out]   error residual
    \param  TotalErrorExactSol [out]   error in xh

*/
#ifdef HAVE_TRILINOSCOUPLINGS_STRATIMIKOS
void TestMultiLevelPreconditioner_Stratimikos(char ProblemType[],
                                              Teuchos::ParameterList   & MLList,
                                              Epetra_CrsMatrix   & CurlCurl,
                                              Epetra_CrsMatrix   & D0clean,
                                              Epetra_CrsMatrix   & M0inv,
                                              Epetra_CrsMatrix   & Ms,
                                              Epetra_CrsMatrix   & M1,
                                              Epetra_MultiVector & xh,
                                              Epetra_MultiVector & b,
                                              double & TotalErrorResidual,
                                              double & TotalErrorExactSol);

  #if defined(HAVE_MUELU_EPETRA) and defined(HAVE_TRILINOSCOUPLINGS_MUELU)
/** \brief  MueLu Preconditioner

    \param  ProblemType        [in]    problem type
    \param  MLList             [in]    ML parameter list
    \param  CurlCurl           [in]    H(curl) stiffness matrix
    \param  D0clean            [in]    Edge to node stiffness matrix
    \param  M0inv              [in]    H(grad) mass matrix inverse
    \param  Ms                 [in]    H(curl) mass matrix w/ sigma
    \param  M1                 [in]    H(curl) mass matrix w/o sigma
    \param  xh                 [out]   solution vector
    \param  b                  [in]    right-hand-side vector
    \param  TotalErrorResidual [out]   error residual
    \param  TotalErrorExactSol [out]   error in xh

*/
void TestPreconditioner_Stratimikos(char ProblemType[],
                                    Teuchos::ParameterList & SList,
                                    Epetra_CrsMatrix   & CurlCurl,
                                    Epetra_CrsMatrix   & D0clean,
                                    Epetra_CrsMatrix   & M0inv,
                                    Epetra_CrsMatrix   & Ms,
                                    Epetra_CrsMatrix   & M1,
                                    Epetra_MultiVector & coords,
                                    Epetra_Vector      & Nx,
                                    Epetra_Vector      & Ny,
                                    Epetra_Vector      & Nz,
                                    Epetra_MultiVector & xh,
                                    Epetra_MultiVector & b,
                                    double & TotalErrorResidual,
                                    double & TotalErrorExactSol);
  #endif
#endif

/**********************************************************************************/
/******** FUNCTION DECLARATIONS FOR EXACT SOLUTION AND SOURCE TERMS ***************/
/**********************************************************************************/

/** \brief  Exact solution evaluation.

    \param  uExact0            [out]   first component of exact solution at (x,y,z)
    \param  uExact1            [out]   second component of exact solution at (x,y,z)
    \param  uExact2            [out]   third component of exact solution at (x,y,z)
    \param  x                  [in]    x coordinate
    \param  y                  [in]    y coordinate
    \param  z                  [in]    z coordinate
*/
int evalu(double & uExact0,
          double & uExact1,
          double & uExact2,
          double & x,
          double & y,
          double & z);

/** \brief  Curl of exact solution.

    \param  curlu0         [out]   first component of curl of exact solution
    \param  curlu1         [out]   second component of curl of exact solution
    \param  curlu2         [out]   third component of curl of exact solution
    \param  x                  [in]    x coordinate
    \param  y                  [in]    y coordinate
    \param  z                  [in]    z coordinate
    \param  mu                 [in]    material parameter
*/
int evalCurlu(double & curlu0,
              double & curlu1,
              double & curlu2,
              double & x,
              double & y,
              double & z,
              double & mu);

/** \brief  CurlCurl of exact solution.

    \param  curlcurlu0         [out]   first component of curl-curl of exact solution
    \param  curlcurlu1         [out]   second component of curl-curl of exact solution
    \param  curlcurlu2         [out]   third component of curl-curl of exact solution
    \param  x                  [in]    x coordinate
    \param  y                  [in]    y coordinate
    \param  z                  [in]    z coordinate
    \param  mu                 [in]    material parameter
*/
int evalCurlCurlu(double & curlcurlu0,
                  double & curlcurlu1,
                  double & curlcurlu2,
                  double & x,
                  double & y,
                  double & z,
                  double & mu);

/**********************************************************************************/
/**********************************************************************************/
/**********************************************************************************/



/**********************************************************************************/
/******************************** MAIN ********************************************/
/**********************************************************************************/
int main(int argc, char *argv[]) {
  // using namespace TrilinosCouplings;
  // using IntrepidPoissonExample::parseCommandLineArguments;
  // using IntrepidPoissonExample::setCommandLineArgumentDefaults;
  // using IntrepidPoissonExample::setMaterialTensorOffDiagonalValue;
  // using IntrepidPoissonExample::setUpCommandLineArguments;
  using Teuchos::CommandLineProcessor;

  int error = 0;
#ifdef HAVE_MPI
  Teuchos::GlobalMPISession mpiSession(&argc, &argv,0);
  int rank=mpiSession.getRank();
  int numProcs=mpiSession.getNProc();
  Epetra_MpiComm Comm(MPI_COMM_WORLD);
  int MyPID = Comm.MyPID();
#else
  int rank=0;
  int numProcs=1;
  int MyPID = 0;
  Epetra_SerialComm Comm;
#endif
  Epetra_Time Time(Comm);


  // Did the user specify --help at the command line to print help
  // with command-line arguments?
  bool printedHelp = false;
  // Values of command-line arguments.
  int nx, ny, nz;
  std::string xmlInFileName, xmlStratimikos;
  bool verbose, debug, jiggle, dump;
  std::string solverName;
  double scaling = 1.0;

  // Set default values of command-line arguments.
  nx = 10;
  ny = 10;
  nz = 10;
  xmlInFileName = "Maxwell.xml";
  xmlStratimikos = "stratimikos.xml";
  solverName = "default";
  verbose = false;
  debug = false;
  jiggle = false;
  dump = false;
  // Parse and validate command-line arguments.
  Teuchos::CommandLineProcessor cmdp (false, true);
  cmdp.setOption ("nx", &nx, "Number of cells along the x dimension");
  cmdp.setOption ("ny", &ny, "Number of cells along the y dimension");
  cmdp.setOption ("nz", &nz, "Number of cells along the z dimension");
  cmdp.setOption ("inputParams", &xmlInFileName, "XML file of input "
                  "parameters, which we read if specified and not \"\".  "
                  "If it has a \"meshInput\" parameter, we use its "
                  "std::string value as the Pamgen mesh specification.  "
                  "Otherwise, we tell Pamgen to make a cube, using "
                  "nx, ny, and nz.");
  cmdp.setOption ("stratimikosParams", &xmlStratimikos, "XML file with Stratimikos inputs");
  cmdp.setOption ("solverName", &solverName, "Name of iterative linear solver "
                  "to use for solving the linear system.  Examples include "
                  "\"ML\", \"MueLu\", \"ML-Stratimikos\" and \"Stratimikos\".");
  cmdp.setOption ("verbose", "quiet", &verbose,
                  "Whether to print verbose status output.");
  cmdp.setOption ("debug", "release", &debug,
                  "Whether to print copious debugging output to stderr.");
  cmdp.setOption ("scaling", &scaling, "scale mass matrix");
  cmdp.setOption ("jiggle", "nojiggle", &jiggle,
                  "Whether to randomly perturb the mesh.");
  cmdp.setOption ("dump", "nodump", &dump,
                  "Whether to dump data.");

  if (MyPID == 0) {
    std::cout                                                           \
      << "===============================================================================\n" \
      << "|                                                                             |\n" \
      << "|    Example: Solve Div-Curl System on Hexahedral Mesh                        |\n" \
      << "|               with Curl-conforming Elements                                 |\n" \
      << "|                                                                             |\n" \
      << "|  Questions? Contact  Pavel Bochev  (pbboche@sandia.gov),                    |\n" \
      << "|                      Denis Ridzal  (dridzal@sandia.gov),                    |\n" \
      << "|                      Kara Peterson (kjpeter@sandia.gov).                    |\n" \
      << "|                                                                             |\n" \
      << "|  Intrepid's website: http://trilinos.sandia.gov/packages/intrepid           |\n" \
      << "|  Pamgen's website:   http://trilinos.sandia.gov/packages/pamgen             |\n" \
      << "|  ML's website:       http://trilinos.sandia.gov/packages/ml                 |\n" \
      << "|  Trilinos website:   http://trilinos.sandia.gov                             |\n" \
      << "|                                                                             |\n" \
      << "===============================================================================\n";
  }

  const CommandLineProcessor::EParseCommandLineReturn parseResult =
    cmdp.parse (argc, argv);
  if (parseResult == CommandLineProcessor::PARSE_HELP_PRINTED) {
    printedHelp = true;
  }
  else {
    printedHelp = false;
    TEUCHOS_TEST_FOR_EXCEPTION(
                               parseResult != CommandLineProcessor::PARSE_SUCCESSFUL,
                               std::invalid_argument, "Failed to parse command-line arguments.");
    TEUCHOS_TEST_FOR_EXCEPTION(
                               xmlInFileName == "" && (nx <= 0 || ny <= 0 || nz <= 0),
                               std::invalid_argument, "If no XML parameters filename is specified (via "
                               "--inputParams), then the number of cells along each dimension of the "
                               "mesh (--nx, --ny, and --nz) must be positive.");
  }

  if (printedHelp) {
    // The user specified --help at the command line to print help
    // with command-line arguments.  We printed help already, so quit
    // with a happy return code.
    return EXIT_SUCCESS;
  }

  long long *  node_comm_proc_ids   = NULL;
  long long *  node_cmap_node_cnts  = NULL;
  long long *  node_cmap_ids        = NULL;
  long long ** comm_node_ids        = NULL;
  long long ** comm_node_proc_ids   = NULL;

  std::set < topo_entity * , fecomp > edge_set;
  std::set < topo_entity * , fecomp > face_set;

  std::vector < topo_entity * > edge_vector;
  std::vector < topo_entity * > face_vector;

  std::vector < int > edge_comm_procs;


  /**********************************************************************************/
  /********************************** GET XML INPUTS ********************************/
  /**********************************************************************************/

  // Read xml file into parameter list
  Teuchos::ParameterList inputList;

  if(xmlInFileName.length()) {
    if (MyPID == 0) {
      std::cout << "\nReading parameter list from the XML file \""<<xmlInFileName<<"\" ...\n\n";
    }
    Teuchos::updateParametersFromXmlFile(xmlInFileName,Teuchos::ptr (&inputList));
    if (MyPID == 0) {
      inputList.print(std::cout,2,true,true);
      std::cout << "\n";
    }
  }
  // Get pamgen mesh definition
  std::string meshInput =                       \
    "mesh                         \n"
    "  brick                      \n"
    "   zmin = 0.0                \n"
    "   xmin = 0.0                \n"
    "   ymin = 0.0                \n"
    "   numz 1                    \n"
    "     zblock 1 1.0 interval " + std::to_string(nz) + "\n" +
    "   numx 1                    \n"
    "     xblock 1 1.0 interval " + std::to_string(nx) + "\n" +
    "   numy 1                    \n"
    "     yblock 1 1.0 interval " + std::to_string(ny) + "\n" +
    "  end                        \n"
    "  set assign                 \n"
    "     sideset, ilo, 1         \n"
    "     sideset, jlo, 2         \n"
    "     sideset, klo, 3         \n"
    "     sideset, ihi, 4         \n"
    "     sideset, jhi, 5         \n"
    "     sideset, khi, 6         \n"
    "  end                        \n"
    "end                          \n";
  meshInput = inputList.get("meshInput", meshInput);

  // Get the solver name from input deck or command line
  if(solverName == "default") {
    if(inputList.isParameter("Preconditioner")) 
      solverName=inputList.get("Preconditioner","MueLu");
    else
      solverName="MueLu";
  }  





  /**********************************************************************************/
  /***************************** GET CELL TOPOLOGY **********************************/
  /**********************************************************************************/

  // Get cell topology for base hexahedron
  shards::CellTopology cellType(shards::getCellTopologyData<shards::Hexahedron<8> >() );

  // Get dimensions
  int numNodesPerElem = cellType.getNodeCount();
  int numEdgesPerElem = cellType.getEdgeCount();
  int numFacesPerElem = cellType.getSideCount();
  int numNodesPerEdge = 2;
  int numNodesPerFace = 4;
  int numEdgesPerFace = 4;
  int spaceDim = cellType.getDimension();

  // Build reference element edge to node map
  FieldContainer<int> refEdgeToNode(numEdgesPerElem,numNodesPerEdge);
  for (int i=0; i<numEdgesPerElem; i++){
    refEdgeToNode(i,0)=cellType.getNodeMap(1, i, 0);
    refEdgeToNode(i,1)=cellType.getNodeMap(1, i, 1);
  }

  // Build reference element face to node map
  FieldContainer<int> refFaceToNode(numFacesPerElem,numNodesPerFace);
  for (int i=0; i<numFacesPerElem; i++){
    refFaceToNode(i,0)=cellType.getNodeMap(2, i, 0);
    refFaceToNode(i,1)=cellType.getNodeMap(2, i, 1);
    refFaceToNode(i,2)=cellType.getNodeMap(2, i, 2);
    refFaceToNode(i,3)=cellType.getNodeMap(2, i, 3);
  }

  // Build reference element face to edge map (Hardcoded for now)
  FieldContainer<int> refFaceToEdge(numFacesPerElem,numEdgesPerFace);
  refFaceToEdge(0,0)=0; refFaceToEdge(0,1)=9;
  refFaceToEdge(0,2)=4; refFaceToEdge(0,3)=8;
  refFaceToEdge(1,0)=1; refFaceToEdge(1,1)=10;
  refFaceToEdge(1,2)=5; refFaceToEdge(1,3)=9;
  refFaceToEdge(2,0)=2; refFaceToEdge(2,1)=11;
  refFaceToEdge(2,2)=6; refFaceToEdge(2,3)=10;
  refFaceToEdge(3,0)=3; refFaceToEdge(3,1)=8;
  refFaceToEdge(3,2)=7; refFaceToEdge(3,3)=11;
  refFaceToEdge(4,0)=0; refFaceToEdge(4,1)=1;
  refFaceToEdge(4,2)=2; refFaceToEdge(4,3)=3;
  refFaceToEdge(5,0)=4; refFaceToEdge(5,1)=5;
  refFaceToEdge(5,2)=6; refFaceToEdge(5,3)=7;


  /**********************************************************************************/
  /******************************* GENERATE MESH ************************************/
  /**********************************************************************************/


  if (MyPID == 0) {
    std::cout << "Generating mesh ... \n\n";
  }

  // Generate mesh with Pamgen
  int dim=3;
  long long maxInt = 9223372036854775807LL;
  long long cr_result = Create_Pamgen_Mesh(meshInput.c_str(), dim, rank, numProcs, maxInt);
  TrilinosCouplings::pamgen_error_check(std::cout,cr_result);

  // Get local mesh size info
  char title[100];
  long long numDim;
  long long numNodes;
  long long numElems;
  long long numElemBlk;
  long long numNodeSets;
  long long numSideSets;
  int id = 0;

  im_ex_get_init_l(id, title, &numDim, &numNodes,
                   &numElems, &numElemBlk, &numNodeSets,
                   &numSideSets);

  // Get global mesh size info
  long long numNodesGlobal;
  long long numElemsGlobal;
  long long numElemBlkGlobal;
  long long numNodeSetsGlobal;
  long long numSideSetsGlobal;

  im_ne_get_init_global_l(id, &numNodesGlobal, &numElemsGlobal,
                          &numElemBlkGlobal, &numNodeSetsGlobal,
                          &numSideSetsGlobal);
#ifdef HAVE_XPETRA_EPETRA
  MachineLearningStatistics_Hex3D<double, int, int, Xpetra::EpetraNode> MLStatistics(numElemsGlobal);
#endif

  long long * block_ids = new long long [numElemBlk];
  error += im_ex_get_elem_blk_ids_l(id, block_ids);


  long long  *nodes_per_element   = new long long [numElemBlk];
  long long  *element_attributes  = new long long [numElemBlk];
  long long  *elements            = new long long [numElemBlk];
  char      **element_types       = new char * [numElemBlk];
  long long **elmt_node_linkage   = new long long * [numElemBlk];


  for(long long i = 0; i < numElemBlk; i ++){
    element_types[i] = new char [MAX_STR_LENGTH + 1];
    error += im_ex_get_elem_block_l(id,
                                    block_ids[i],
                                    element_types[i],
                                    (long long*)&(elements[i]),
                                    (long long*)&(nodes_per_element[i]),
                                    (long long*)&(element_attributes[i]));
  }

  /*connectivity*/
  for(long long b = 0; b < numElemBlk; b++){
    elmt_node_linkage[b] =  new long long [nodes_per_element[b]*elements[b]];
    error += im_ex_get_elem_conn_l(id,block_ids[b],elmt_node_linkage[b]);
  }

  // Get mu value for each block of elements from parameter list
  double  *mu = new double [numElemBlk];
  for(int b = 0; b < numElemBlk; b++){
    stringstream muBlock;
    muBlock.clear();
    muBlock << "mu" << b;
    mu[b] = inputList.get(muBlock.str(),1.0);
  }

  // Get mu value for each block of elements from parameter list
  double  *sigma = new double [numElemBlk];
  for(int b = 0; b < numElemBlk; b++){
    stringstream sigmaBlock;
    sigmaBlock.clear();
    sigmaBlock << "sigma" << b;
    sigma[b] = inputList.get(sigmaBlock.str(),1.0);
  }

  // Get node-element connectivity and set element mu/sigma value
  int telct = 0;
  FieldContainer<int> elemToNode(numElems,numNodesPerElem);
  FieldContainer<double> muVal(numElems);
  FieldContainer<double> sigmaVal(numElems);

  for(long long b = 0; b < numElemBlk; b++){
    for(long long el = 0; el < elements[b]; el++){
      for (int j=0; j<numNodesPerElem; j++) {
        elemToNode(telct,j) = elmt_node_linkage[b][el*numNodesPerElem + j]-1;
      }
      muVal(telct) = mu[b];
      sigmaVal(telct) = sigma[b];
      
      telct ++;
    }
  }

  // Read node coordinates and place in field container
  FieldContainer<double> nodeCoord(numNodes,dim);
  double * nodeCoordx = new double [numNodes];
  double * nodeCoordy = new double [numNodes];
  double * nodeCoordz = new double [numNodes];
  im_ex_get_coord_l(id,nodeCoordx,nodeCoordy,nodeCoordz);
  for (int i=0; i<numNodes; i++) {
    nodeCoord(i,0)=nodeCoordx[i];
    nodeCoord(i,1)=nodeCoordy[i];
    nodeCoord(i,2)=nodeCoordz[i];
  }

  /*parallel info*/
  long long num_internal_nodes;
  long long num_border_nodes;
  long long num_external_nodes;
  long long num_internal_elems;
  long long num_border_elems;
  long long num_node_comm_maps;
  long long num_elem_comm_maps;
  im_ne_get_loadbal_param_l( id,
                             &num_internal_nodes,
                             &num_border_nodes,
                             &num_external_nodes,
                             &num_internal_elems,
                             &num_border_elems,
                             &num_node_comm_maps,
                             &num_elem_comm_maps,
                             0/*unused*/ );

  if(num_node_comm_maps > 0){
    node_comm_proc_ids   = new long long  [num_node_comm_maps];
    node_cmap_node_cnts  = new long long  [num_node_comm_maps];
    node_cmap_ids        = new long long  [num_node_comm_maps];
    comm_node_ids        = new long long* [num_node_comm_maps];
    comm_node_proc_ids   = new long long* [num_node_comm_maps];

    long long *  elem_cmap_ids        = new long long [num_elem_comm_maps];
    long long *  elem_cmap_elem_cnts  = new long long [num_elem_comm_maps];
    if ( im_ne_get_cmap_params_l( id,
                                  node_cmap_ids,
                                  (long long*)node_cmap_node_cnts,
                                  elem_cmap_ids,
                                  (long long*)elem_cmap_elem_cnts,
                                  0/*not used proc_id*/ ) < 0 )++error;

    for(long long j = 0; j < num_node_comm_maps; j++) {
      comm_node_ids[j]       = new long long [node_cmap_node_cnts[j]];
      comm_node_proc_ids[j]  = new long long [node_cmap_node_cnts[j]];
      if ( im_ne_get_node_cmap_l( id,
                                  node_cmap_ids[j],
                                  comm_node_ids[j],
                                  comm_node_proc_ids[j],
                                  0/*not used proc_id*/ ) < 0 )++error;
      node_comm_proc_ids[j] = comm_node_proc_ids[j][0];
    }
  }



  //Calculate global node ids
  long long * globalNodeIds = new long long[numNodes];
  bool * nodeIsOwned = new bool[numNodes];

  calc_global_node_ids(globalNodeIds,
                       nodeIsOwned,
                       numNodes,
                       num_node_comm_maps,
                       node_cmap_node_cnts,
                       node_comm_proc_ids,
                       comm_node_ids,
                       rank);

  // Count owned nodes
  int ownedNodes=0;
  for(int i=0;i<numNodes;i++)
    if(nodeIsOwned[i]) ownedNodes++;

  // Build a list of the OWNED global ids...
  int *ownedGIDs=new int [ownedNodes];
  int oidx=0;
  for(int i=0;i<numNodes;i++)
    if(nodeIsOwned[i]){
      ownedGIDs[oidx]=(int)globalNodeIds[i];
      oidx++;
    }

  FieldContainer<int> elemToEdge(numElems,numEdgesPerElem);
  FieldContainer<int> elemToFace(numElems,numFacesPerElem);

  // Calculate edge and face ids
  int elct = 0;
  for(long long b = 0; b < numElemBlk; b++){
    if(nodes_per_element[b] == 4){
    }
    else if (nodes_per_element[b] == 8){
      //loop over all elements and push their edges onto a set if they are not there already
      for(long long el = 0; el < elements[b]; el++){
        std::set< topo_entity *, fecomp > ::iterator fit;
        for (int i=0; i < numEdgesPerElem; i++){
          topo_entity * teof = new topo_entity;
          for(int j = 0; j < numNodesPerEdge;j++){
            teof->add_node(elmt_node_linkage[b][el*numNodesPerElem + refEdgeToNode(i,j)],globalNodeIds);
          }
          teof->sort();
          fit = edge_set.find(teof);
          if(fit == edge_set.end()){
            teof->local_id = edge_vector.size();
            edge_set.insert(teof);
            elemToEdge(elct,i)= edge_vector.size();
            edge_vector.push_back(teof);
          }
          else{
            elemToEdge(elct,i) = (*fit)->local_id;
            delete teof;
          }
        }
        for (int i=0; i < numFacesPerElem; i++){
          topo_entity * teof = new topo_entity;
          for(int j = 0; j < numNodesPerFace;j++){
            teof->add_node(elmt_node_linkage[b][el*numNodesPerElem + refFaceToNode(i,j)],globalNodeIds);
          }
          teof->sort();
          fit = face_set.find(teof);
          if(fit == face_set.end()){
            teof->local_id = face_vector.size();
            face_set.insert(teof);
            elemToFace(elct,i)= face_vector.size();
            face_vector.push_back(teof);
          }
          else{
            elemToFace(elct,i) = (*fit)->local_id;
            delete teof;
          }
        }
        elct ++;
      }
    }
  }

  // Edge to Node connectivity
  FieldContainer<int> edgeToNode(edge_vector.size(), numNodesPerEdge);
  for(unsigned ect = 0; ect != edge_vector.size(); ect++){
    std::list<long long>::iterator elit;
    int nct = 0;
    for(elit  = edge_vector[ect]->local_node_ids.begin();
        elit != edge_vector[ect]->local_node_ids.end();
        elit ++){
      edgeToNode(ect,nct) = *elit-1;
      nct++;
    }
  }

  // Face to Node connectivity
  FieldContainer<int> faceToNode(face_vector.size(), numNodesPerFace);
  for(unsigned fct = 0; fct != face_vector.size(); fct++){
    std::list<long long>::iterator flit;
    int nct = 0;
    for(flit  = face_vector[fct]->local_node_ids.begin();
        flit != face_vector[fct]->local_node_ids.end();
        flit ++){
      faceToNode(fct,nct) = *flit-1;
      nct++;
    }
  }

  // Face to Edge connectivity
  FieldContainer<int> faceToEdge(face_vector.size(), numEdgesPerFace);
  FieldContainer<bool> faceDone(face_vector.size());
  for (int ielem = 0; ielem < numElems; ielem++){
    for (int iface = 0; iface < numFacesPerElem; iface++){
      if (!faceDone(elemToFace(ielem,iface))){
        for (int iedge = 0; iedge < numEdgesPerFace; iedge++){
          faceToEdge(elemToFace(ielem,iface),iedge) =
            elemToEdge(ielem,refFaceToEdge(iface,iedge));
          faceDone(elemToFace(ielem,iface))=1;
        }
      }
    }
  }

  int numEdges = edge_vector.size();
  int numFaces = face_vector.size();

  // Calculate global edge and face numbering
  std::string doing_type;
  doing_type = "EDGES";
  calc_global_ids(edge_vector,
                  comm_node_ids,
                  node_comm_proc_ids,
                  node_cmap_node_cnts,
                  num_node_comm_maps,
                  rank,
                  doing_type);


  doing_type = "FACES";
  calc_global_ids(face_vector,
                  comm_node_ids,
                  node_comm_proc_ids,
                  node_cmap_node_cnts,
                  num_node_comm_maps,
                  rank,
                  doing_type);

  // Build list of owned global edge ids
  long long * globalEdgeIds = new long long[numEdges];
  bool * edgeIsOwned = new bool[numEdges];
  int numOwnedEdges=0;
  for (int i=0; i<numEdges; i++) {
    edgeIsOwned[i] = edge_vector[i]->owned;
    globalEdgeIds[i] = edge_vector[i]->global_id;
    if (edgeIsOwned[i]){
      numOwnedEdges++;
    }
  }
  int * ownedEdgeIds = new int[numOwnedEdges];
  int nedge=0;
  for (int i=0; i<numEdges; i++) {
    if (edgeIsOwned[i]){
      ownedEdgeIds[nedge]=(int)globalEdgeIds[i];
      nedge++;
    }
  }

  // Build list of owned global face ids
  long long * globalFaceIds = new long long[numFaces];
  bool * faceIsOwned = new bool[numFaces];
  int numOwnedFaces=0;
  for (int i=0; i<numFaces; i++) {
    faceIsOwned[i] = face_vector[i]->owned;
    globalFaceIds[i] = face_vector[i]->global_id;
    if (faceIsOwned[i]){
      numOwnedFaces++;
    }
  }
  int * ownedFaceIds = new int[numOwnedFaces];
  int nface=0;
  for (int i=0; i<numFaces; i++) {
    if (faceIsOwned[i]){
      ownedFaceIds[nface]=(int)globalFaceIds[i];
      nface++;
    }
  }

  // Calculate number of global edges and faces
  int numEdgesGlobal;
  int numFacesGlobal;
#ifdef HAVE_MPI
  Comm.SumAll(&numOwnedEdges,&numEdgesGlobal,1);
  Comm.SumAll(&numOwnedFaces,&numFacesGlobal,1);
#else
  numEdgesGlobal = numEdges;
  numFacesGlobal = numFaces;
#endif


  // Print mesh size information
  if (MyPID == 0) {
    std::cout << " Number of Elements: " << numElemsGlobal << " \n";
    std::cout << "    Number of Nodes: " << numNodesGlobal << " \n";
    std::cout << "    Number of Edges: " << numEdgesGlobal << " \n";
    std::cout << "    Number of Faces: " << numFacesGlobal << " \n\n";
  }



  // Container indicating whether a face is on the boundary (1-yes 0-no)
  FieldContainer<int> edgeOnBoundary(numEdges);
  FieldContainer<int> faceOnBoundary(numFaces);

  // Get boundary (side set) information
  long long * sideSetIds = new long long [numSideSets];
  long long numSidesInSet;
  long long numDFinSet;
  im_ex_get_side_set_ids_l(id,sideSetIds);
  for (int i=0; i<numSideSets; i++) {
    im_ex_get_side_set_param_l(id,sideSetIds[i],&numSidesInSet,&numDFinSet);
    if (numSidesInSet > 0){
      long long * sideSetElemList = new long long [numSidesInSet];
      long long * sideSetSideList = new long long [numSidesInSet];
      im_ex_get_side_set_l(id,sideSetIds[i],sideSetElemList,sideSetSideList);
      for (int j=0; j<numSidesInSet; j++) {
        int iface = sideSetSideList[j]-1;
        faceOnBoundary(elemToFace(sideSetElemList[j]-1,iface))=1;
        edgeOnBoundary(faceToEdge(elemToFace(sideSetElemList[j]-1,iface),0))=1;
        edgeOnBoundary(faceToEdge(elemToFace(sideSetElemList[j]-1,iface),1))=1;
        edgeOnBoundary(faceToEdge(elemToFace(sideSetElemList[j]-1,iface),2))=1;
        edgeOnBoundary(faceToEdge(elemToFace(sideSetElemList[j]-1,iface),3))=1;
      }
      delete [] sideSetElemList;
      delete [] sideSetSideList;
    }
  }

  delete [] sideSetIds;


  // Container indicating whether a node is on the boundary (1-yes 0-no)
  FieldContainer<int> nodeOnBoundary(numNodes);
  int numEdgeOnBndy=0;
  for (int i=0; i<numEdges; i++) {
    if (edgeOnBoundary(i)){
      nodeOnBoundary(edgeToNode(i,0))=1;
      nodeOnBoundary(edgeToNode(i,1))=1;
      numEdgeOnBndy++;
    }
  }

  // Define global epetra maps
  Epetra_Map globalMapG(-1,ownedNodes,ownedGIDs,0,Comm);
  Epetra_Map globalMapC(-1,numOwnedEdges,ownedEdgeIds,0,Comm);
  
  if (jiggle) {
    /***********************************************************************************/
    /* This block of code will create a randomly perturbed mesh by jiggling the node   */
    /* coordinates away from their initial location.                                   */
    /***********************************************************************************/

    if(MyPID==0) {std::cout << "Jiggling the mesh" << std::endl << std::endl;}

    // Build an overlapping map. To match globalMapG, we need to convert to int.
    int * globalNodeIdsInt = new int[numNodes];
    for (int i=0; i<numNodes; i++)
      globalNodeIdsInt[i]=(int)globalNodeIds[i];
    Epetra_Map globalMapAllNodes(-1,numNodes,globalNodeIdsInt,0,Comm);

    // get random numbers between -1.0 and 1.0 wrt to 1-to-1 map
    Epetra_MultiVector displacementTemp(globalMapG,3);
    displacementTemp.Random();

    // import that to overlapping map
    Epetra_MultiVector displacement(globalMapAllNodes,3);
    Epetra_Export MyImporter(globalMapAllNodes,globalMapG);
    displacement.Import(displacementTemp,MyImporter,Insert);

    // Side length assuming an initially regular grid
    double hx = nodeCoord(elemToNode(0,1),0)-nodeCoord(elemToNode(0,0),0);
    double hy = nodeCoord(elemToNode(0,3),1)-nodeCoord(elemToNode(0,0),1);
    double hz = nodeCoord(elemToNode(0,4),2)-nodeCoord(elemToNode(0,0),2);

    const double fac = 0.05;

    // Loop over nodes
    for (int inode = 0; inode < numNodes; inode++){
      if (!nodeOnBoundary(inode)) {
        double rx = displacement[0][inode];
        double ry = displacement[1][inode];
        double rz = displacement[2][inode]; 

        nodeCoord(inode,0) = nodeCoord(inode,0) + fac * hx * rx;
        nodeCoord(inode,1) = nodeCoord(inode,1) + fac * hy * ry;
        nodeCoord(inode,2) = nodeCoord(inode,2) + fac * hz * rz;
        nodeCoordx[inode] = nodeCoord(inode,0);
        nodeCoordy[inode] = nodeCoord(inode,1);
        nodeCoordz[inode] = nodeCoord(inode,2);
      }
    }
  }

#ifdef HAVE_XPETRA_EPETRA
  // Statistics: Phase 1
  MLStatistics.Phase1(elemToNode,elemToEdge,edgeToNode,nodeCoord,sigmaVal);
#endif

  /**********************************************************************************/
  /********************************* GET CUBATURE ***********************************/
  /**********************************************************************************/

  // Get numerical integration points and weights for cell
  DefaultCubatureFactory<double>  cubFactory;
  int cubDegree = 2;
  Teuchos::RCP<Cubature<double> > hexCub = cubFactory.create(cellType, cubDegree);

  int cubDim       = hexCub->getDimension();
  int numCubPoints = hexCub->getNumPoints();

  FieldContainer<double> cubPoints(numCubPoints, cubDim);
  FieldContainer<double> cubWeights(numCubPoints);

  hexCub->getCubature(cubPoints, cubWeights);

  /**********************************************************************************/
  /*     Get numerical integration points and weights for hexahedron face           */
  /*                  (needed for rhs boundary term)                                */
  /**********************************************************************************/

  // Define topology of the face parametrization domain as [-1,1]x[-1,1]
  shards::CellTopology paramQuadFace(shards::getCellTopologyData<shards::Quadrilateral<4> >() );

  // Define cubature
  DefaultCubatureFactory<double>  cubFactoryFace;
  Teuchos::RCP<Cubature<double> > hexFaceCubature = cubFactoryFace.create(paramQuadFace, 3);
  int cubFaceDim    = hexFaceCubature -> getDimension();
  int numFacePoints = hexFaceCubature -> getNumPoints();

  // Define storage for cubature points and weights on [-1,1]x[-1,1]
  FieldContainer<double> paramFaceWeights(numFacePoints);
  FieldContainer<double> paramFacePoints(numFacePoints,cubFaceDim);

  // Define storage for cubature points on workset faces
  hexFaceCubature -> getCubature(paramFacePoints, paramFaceWeights);

  /**********************************************************************************/
  /*     Get numerical integration points and weights for hexahedron edge           */
  /*           (needed for inhomogeneous boundary terms)                            */
  /**********************************************************************************/

  // Define topology of the edge parametrization domain as [-1,1]
  shards::CellTopology paramEdge(shards::getCellTopologyData<shards::Line<2> >() );

  // Define cubature
  DefaultCubatureFactory<double>  cubFactoryEdge;
  Teuchos::RCP<Cubature<double> > hexEdgeCubature = cubFactoryEdge.create(paramEdge, 3);
  int cubEdgeDim    = hexEdgeCubature -> getDimension();
  int numEdgePoints = hexEdgeCubature -> getNumPoints();

  // Define storage for cubature points and weights on [-1,1]
  FieldContainer<double> paramEdgeWeights(numEdgePoints);
  FieldContainer<double> paramEdgePoints(numEdgePoints,cubEdgeDim);

  // Define storage for cubature points on workset faces
  hexEdgeCubature -> getCubature(paramEdgePoints, paramEdgeWeights);

  if(MyPID==0) {std::cout << "Getting cubature                            "
                          << Time.ElapsedTime() << " sec \n"  ; Time.ResetStartTime();}


  /**********************************************************************************/
  /*********************************** GET BASIS ************************************/
  /**********************************************************************************/

  // Define basis
  Basis_HCURL_HEX_I1_FEM<double, FieldContainer<double> > hexHCurlBasis;
  Basis_HGRAD_HEX_C1_FEM<double, FieldContainer<double> > hexHGradBasis;

  int numFieldsC = hexHCurlBasis.getCardinality();
  int numFieldsG = hexHGradBasis.getCardinality();

  // Evaluate basis at cubature points
  FieldContainer<double> HGVals(numFieldsG, numCubPoints);
  FieldContainer<double> HCVals(numFieldsC, numCubPoints, spaceDim);
  FieldContainer<double> HCurls(numFieldsC, numCubPoints, spaceDim);
  FieldContainer<double> worksetCVals(numFieldsC, numFacePoints, spaceDim);

  hexHCurlBasis.getValues(HCVals, cubPoints, OPERATOR_VALUE);
  hexHCurlBasis.getValues(HCurls, cubPoints, OPERATOR_CURL);
  hexHGradBasis.getValues(HGVals, cubPoints, OPERATOR_VALUE);

  if(MyPID==0) {std::cout << "Getting basis                               "
                          << Time.ElapsedTime() << " sec \n"  ; Time.ResetStartTime();}


  /**********************************************************************************/
  /********************* BUILD MAPS FOR GLOBAL SOLUTION *****************************/
  /**********************************************************************************/

  Epetra_FECrsMatrix StiffMatrixC(Copy, globalMapC, numFieldsC);
  Epetra_FECrsMatrix MassMatrixC (Copy, globalMapC, numFieldsC);
  Epetra_FECrsMatrix MassMatrixC1 (Copy, globalMapC, numFieldsC);// No sigma
  Epetra_FECrsMatrix MassMatrixG (Copy, globalMapG, numFieldsG);
  Epetra_FEVector    rhsVector   (globalMapC);

  if(MyPID==0) {std::cout << "Build global maps                           "
                          << Time.ElapsedTime() << " sec \n";  Time.ResetStartTime();}


  /**********************************************************************************/
  /************************** OUTPUT CONNECTIVITY (FOR PLOTTING) ********************/
  /**********************************************************************************/


  // Build the coordinate vectors for ML solver (including owned nodes only)
  Epetra_Vector Nx(globalMapG), Ny(globalMapG),Nz(globalMapG);
  for(int i=0,nlid=0;i<numNodes;i++)
    if(nodeIsOwned[i]) {
      Nx[nlid]=nodeCoordx[i];
      Ny[nlid]=nodeCoordy[i];
      Nz[nlid]=nodeCoordz[i];
      nlid++;
    }


  Epetra_Map globalMapElem(numElemsGlobal, numElems, 0, Comm);

  // Put coordinates in multivector for output
  Epetra_MultiVector nCoord(globalMapG,3);
  
  int ownedNode = 0;
  for (int inode=0; inode<numNodes; inode++) {
    if (nodeIsOwned[inode]) {
      nCoord[0][ownedNode]=nodeCoord(inode,0);
      nCoord[1][ownedNode]=nodeCoord(inode,1);
      nCoord[2][ownedNode]=nodeCoord(inode,2);
      ownedNode++;
    }
  }
  if (dump){  
    EpetraExt::MultiVectorToMatrixMarketFile("coords.dat",nCoord,0,0,false);

    // Put element to node mapping in multivector for output
    Epetra_MultiVector elem2node(globalMapElem, numNodesPerElem);
    for (int ielem=0; ielem<numElems; ielem++) {
      for (int inode=0; inode<numNodesPerElem; inode++) {
        elem2node[inode][ielem]=globalNodeIds[elemToNode(ielem,inode)];
      }
    }
    EpetraExt::MultiVectorToMatrixMarketFile("elem2node.dat",elem2node,0,0,false);

    // Put element to edge mapping in multivector for output
    Epetra_MultiVector elem2edge(globalMapElem, numEdgesPerElem);
    for (int ielem=0; ielem<numElems; ielem++) {
      for (int iedge=0; iedge<numEdgesPerElem; iedge++) {
        elem2edge[iedge][ielem]=globalEdgeIds[elemToEdge(ielem,iedge)];
      }
    }
    EpetraExt::MultiVectorToMatrixMarketFile("elem2edge.dat",elem2edge,0,0,false);

    // Put edge to node mapping in multivector for output
    Epetra_MultiVector edge2node(globalMapC, numNodesPerEdge);
    int ownedEdge = 0;
    for (int iedge=0; iedge<numEdges; iedge++) {
      if (edgeIsOwned[iedge]) {
        for (int inode=0; inode<numNodesPerEdge; inode++) {
          edge2node[inode][ownedEdge]=globalNodeIds[edgeToNode(iedge,inode)];
        }
        ownedEdge++;
      }
    }
    EpetraExt::MultiVectorToMatrixMarketFile("edge2node.dat",edge2node,0,0,false);

    
    if(MyPID==0) {Time.ResetStartTime();}
  }
  
    
  // Define multi-vector for cell edge sign (fill during cell loop)
  Epetra_MultiVector edgeSign(globalMapElem, numEdgesPerElem);


  /**********************************************************************************/
  /*************************BUILD INCIDENCE MATRIX***********************************/
  /**********************************************************************************/

  // Edge to node incidence matrix
  Epetra_FECrsMatrix DGrad(Copy, globalMapC, 2);

  // Estimate the global CFL based on minimum edge length and assumed dt=1"

  // Grab edge coordinates (for dumping to disk)
  Epetra_Vector EDGE_X(globalMapC);
  Epetra_Vector EDGE_Y(globalMapC);
  Epetra_Vector EDGE_Z(globalMapC);

  double vals[2];
  vals[0]=-1.0; vals[1]=1.0;
  for (int j=0, elid=0; j<numEdges; j++){
    if (edgeIsOwned[j]){
      int rowNum = globalEdgeIds[j];
      int colNum[2];
      colNum[0] = globalNodeIds[edgeToNode(j,0)];
      colNum[1] = globalNodeIds[edgeToNode(j,1)];
      DGrad.InsertGlobalValues(1, &rowNum, 2, colNum, vals);
      EDGE_X[elid] = (nodeCoordx[edgeToNode(j,0)] + nodeCoordx[edgeToNode(j,1)])/2.0;
      EDGE_Y[elid] = (nodeCoordy[edgeToNode(j,0)] + nodeCoordy[edgeToNode(j,1)])/2.0;
      EDGE_Z[elid] = (nodeCoordz[edgeToNode(j,0)] + nodeCoordz[edgeToNode(j,1)])/2.0;
      elid++;
    }
  }


  DGrad.GlobalAssemble(globalMapG,globalMapC);
  DGrad.FillComplete(MassMatrixG.RowMap(),MassMatrixC.RowMap());

  if(MyPID==0) {std::cout << "Building incidence matrix                   "
                          << Time.ElapsedTime() << " sec \n"  ; Time.ResetStartTime();}

  // Local CFL Calculations
  double DOUBLE_MAX = std::numeric_limits<double>::max();  
  double l_max_sigma=0.0, l_max_mu = 0.0, l_max_cfl = 0.0, l_max_dx = 0.0, l_max_osm=0.0;
  double l_min_sigma=DOUBLE_MAX, l_min_mu= DOUBLE_MAX, l_min_cfl = DOUBLE_MAX, l_min_dx = DOUBLE_MAX, l_min_osm=DOUBLE_MAX;

  for(long long b = 0, idx=0; b < numElemBlk; b++){
    for(long long el = 0; el < elements[b]; el++){
      l_max_sigma = std::max(l_max_sigma,sigmaVal(idx));
      l_min_sigma = std::min(l_min_sigma,sigmaVal(idx));
      l_max_mu    = std::max(l_max_mu,muVal(idx));
      l_min_mu    = std::min(l_min_mu,muVal(idx));  
      l_max_osm   = std::max(l_max_osm,1/(sigmaVal(idx)*muVal(idx)));
      l_min_osm   = std::min(l_min_osm,1/(sigmaVal(idx)*muVal(idx)));
      
      // We'll chose "dx" as the max/min edge length over the cell
      double my_edge_min = DOUBLE_MAX, my_edge_max=0.0;
      for(int j=0; j<numEdgesPerElem; j++) {
        int edge = elemToEdge(idx,j);
        double my_dx = distance(nodeCoord,edgeToNode(edge,0),edgeToNode(edge,1));
        my_edge_max = std::max(my_edge_max,my_dx);
        my_edge_min = std::min(my_edge_min,my_dx);
      }

      l_max_dx = std::max(l_max_dx,my_edge_max);
      l_min_dx = std::min(l_min_dx,my_edge_min);

      // Note: The max/min's switch here because they're in the denominator
      double my_max_cfl = 1.0 / (sigmaVal(idx) * muVal(idx) * l_min_dx * l_min_dx);
      double my_min_cfl = 1.0 / (sigmaVal(idx) * muVal(idx) * l_max_dx * l_max_dx);
      l_max_cfl = std::max(l_max_cfl,my_max_cfl);
      l_min_cfl = std::min(l_min_cfl,my_min_cfl);
      
      idx++;
    }
  }

  // CFL Range Calculations (assuming a timestep dt=1)
  double g_max_dx, g_min_dx, g_max_mu, g_min_mu, g_max_sigma, g_min_sigma, g_max_cfl, g_min_cfl, g_max_osm, g_min_osm;
  Comm.MaxAll(&l_max_dx,&g_max_dx,1);       Comm.MinAll(&l_min_dx,&g_min_dx,1);
  Comm.MaxAll(&l_max_sigma,&g_max_sigma,1); Comm.MinAll(&l_min_sigma,&g_min_sigma,1);
  Comm.MaxAll(&l_max_mu,&g_max_mu,1);       Comm.MinAll(&l_min_mu,&g_min_mu,1);
  Comm.MaxAll(&l_max_cfl,&g_max_cfl,1);     Comm.MinAll(&l_min_cfl,&g_min_cfl,1);
  Comm.MaxAll(&l_max_osm,&g_max_osm,1);     Comm.MinAll(&l_min_osm,&g_min_osm,1);

  if(MyPID==0) {
    std::cout<<"*** Parameter Ranges ***"<<std::endl;
    std::cout<<"Edge dx Range      : "<<g_min_dx << " to "<<g_max_dx<<std::endl;
    std::cout<<"Sigma Range        : "<<g_min_sigma << " to "<<g_max_sigma<<std::endl;
    std::cout<<"Mu Range           : "<<g_min_mu << " to "<<g_max_mu<<std::endl;
    std::cout<<"1/(Sigma Mu) Range : "<<g_min_osm << " to "<<g_max_osm<<std::endl;
    std::cout<<"Diffusive CFL Range: "<<g_min_cfl<< " to "<<g_max_cfl<<std::endl;

  }


  /**********************************************************************************/
  /******************** INHOMOGENEOUS BOUNDARY CONDITIONS ***************************/
  /**********************************************************************************/

  FieldContainer<double> bndyEdgeVal(numEdgeOnBndy);
  FieldContainer<int>    bndyEdgeToEdge(numEdges);
  FieldContainer<bool>   bndyEdgeDone(numEdges);
  FieldContainer<double> refEdgePoints(numEdgePoints,spaceDim);
  FieldContainer<double> bndyEdgePoints(1,numEdgePoints,spaceDim);
  FieldContainer<double> bndyEdgeJacobians(1,numEdgePoints,spaceDim,spaceDim);
  FieldContainer<double> edgeTan(1,numEdgePoints,spaceDim);
  FieldContainer<double> uDotTangent(numEdgePoints);
  FieldContainer<double> uEdge(numEdgePoints,spaceDim);
  FieldContainer<double> nodes(1, numNodesPerElem, spaceDim);

  int ibedge=0;
  // Evaluate tangent at edge quadrature points
  for (int ielem=0; ielem<numElems; ielem++) {
    for (int inode=0; inode<numNodesPerElem; inode++) {
      nodes(0,inode,0) = nodeCoord(elemToNode(ielem,inode),0);
      nodes(0,inode,1) = nodeCoord(elemToNode(ielem,inode),1);
      nodes(0,inode,2) = nodeCoord(elemToNode(ielem,inode),2);
    }
    for (int iedge=0; iedge<numEdgesPerElem; iedge++){
      if(edgeOnBoundary(elemToEdge(ielem,iedge)) && !bndyEdgeDone(elemToEdge(ielem,iedge))){

        // map evaluation points from reference edge to reference cell
        IntrepidCTools::mapToReferenceSubcell(refEdgePoints,
                                              paramEdgePoints,
                                              1, iedge, cellType);

        // calculate Jacobian
        IntrepidCTools::setJacobian(bndyEdgeJacobians, refEdgePoints,
                                    nodes, cellType);

        // map evaluation points from reference cell to physical cell
        IntrepidCTools::mapToPhysicalFrame(bndyEdgePoints,
                                           refEdgePoints,
                                           nodes, cellType);

        // Compute edge tangents
        IntrepidCTools::getPhysicalEdgeTangents(edgeTan,
                                                bndyEdgeJacobians,
                                                iedge, cellType);

        // evaluate exact solution at edge center and dot with normal
        for(int nPt = 0; nPt < numEdgePoints; nPt++){

          double x = bndyEdgePoints(0, nPt, 0);
          double y = bndyEdgePoints(0, nPt, 1);
          double z = bndyEdgePoints(0, nPt, 2);

          evalu(uEdge(nPt,0), uEdge(nPt,1), uEdge(nPt,2), x, y, z);
          uDotTangent(nPt)=(uEdge(nPt,0)*edgeTan(0,nPt,0)+uEdge(nPt,1)*edgeTan(0,nPt,1)+uEdge(nPt,2)*edgeTan(0,nPt,2));
        }

        // integrate
        for(int nPt = 0; nPt < numEdgePoints; nPt++){
          bndyEdgeVal(ibedge)=bndyEdgeVal(ibedge)+uDotTangent(nPt)*paramEdgeWeights(nPt);
        }

        bndyEdgeToEdge(elemToEdge(ielem,iedge))=ibedge;
        ibedge++;
        bndyEdgeDone(elemToEdge(ielem,iedge))=1;
      }
    }
  }

  // Count of boundary edges
  int numBCEdges=0;
  for (int i=0; i<numEdges; i++){
    if (edgeOnBoundary(i) && edgeIsOwned[i]){
      numBCEdges++;
    }
  }

  // Vector for use in applying BCs
  Epetra_Vector v(globalMapC,true);
  v.PutScalar(0.0);

  // Set v to boundary values on Dirichlet edges
  int * BCEdges = new int [numBCEdges];
  int indbc=0;
  int iOwned=0;
  for (int i=0; i<numEdges; i++){
    if (edgeIsOwned[i]){
      if (edgeOnBoundary(i)){
        BCEdges[indbc]=iOwned;
        indbc++;
        v[iOwned]=bndyEdgeVal(bndyEdgeToEdge(i));
      }
      iOwned++;
    }
  }

  if(numProcs==1 && dump) {
    FILE * f=fopen("boundary_edge_list.dat","w");
    for(int i=0; i<numBCEdges; i++)
      fprintf(f,"%d ",BCEdges[i]);
    fclose(f);
  }


  if(MyPID==0) {std::cout << "Boundary Condition Setup                    "
                          << Time.ElapsedTime() << " sec \n\n"; Time.ResetStartTime();}

  /**********************************************************************************/
  /******************** DEFINE WORKSETS AND LOOP OVER THEM **************************/
  /**********************************************************************************/

  // Define desired workset size and count how many worksets there are on this processor's mesh block
  int desiredWorksetSize = numElems;                      // change to desired workset size!
  //int desiredWorksetSize = 100;                      // change to desired workset size!
  int numWorksets        = numElems/desiredWorksetSize;

  // When numElems is not divisible by desiredWorksetSize, increase workset count by 1
  if(numWorksets*desiredWorksetSize < numElems) numWorksets += 1;

  if (MyPID == 0) {
    std::cout << "Building discretization matrix and right hand side... \n\n";
    std::cout << "\tDesired workset size:                 " << desiredWorksetSize <<"\n";
    std::cout << "\tNumber of worksets (per processor):   " << numWorksets <<"\n\n";
    Time.ResetStartTime();
  }

  for(int workset = 0; workset < numWorksets; workset++){

    // Compute cell numbers where the workset starts and ends
    int worksetSize  = 0;
    int worksetBegin = (workset + 0)*desiredWorksetSize;
    int worksetEnd   = (workset + 1)*desiredWorksetSize;

    // When numElems is not divisible by desiredWorksetSize, the last workset ends at numElems
    worksetEnd   = (worksetEnd <= numElems) ? worksetEnd : numElems;

    // Now we know the actual workset size and can allocate the array for the cell nodes
    worksetSize  = worksetEnd - worksetBegin;
    FieldContainer<double> cellWorkset(worksetSize, numNodesPerElem, spaceDim);
    FieldContainer<double> worksetEdgeSigns(worksetSize, numEdgesPerElem);

    // Copy coordinates into cell workset
    int cellCounter = 0;
    for(int cell = worksetBegin; cell < worksetEnd; cell++){

      // Physical cell coordinates
      for (int inode=0; inode<numNodesPerElem; inode++) {
        cellWorkset(cellCounter,inode,0) = nodeCoord(elemToNode(cell,inode),0);
        cellWorkset(cellCounter,inode,1) = nodeCoord(elemToNode(cell,inode),1);
        cellWorkset(cellCounter,inode,2) = nodeCoord(elemToNode(cell,inode),2);
      }

      // Edge signs
      for (int iedge=0; iedge<numEdgesPerElem; iedge++) {
        if (elemToNode(cell,refEdgeToNode(iedge,0))==edgeToNode(elemToEdge(cell,iedge),0) &&
            elemToNode(cell,refEdgeToNode(iedge,1))==edgeToNode(elemToEdge(cell,iedge),1))
          worksetEdgeSigns(cellCounter,iedge) = 1.0;
        else
          worksetEdgeSigns(cellCounter,iedge) = -1.0;
      }

      // modify signs for edges that are owned by another processor
      // (Note: this is particular to the numbering of edges used in Pamgen!!)
      if (!faceIsOwned[elemToFace(cell,0)]) {
        worksetEdgeSigns(cellCounter,0)=-1.0*worksetEdgeSigns(cellCounter,0);
        worksetEdgeSigns(cellCounter,4)=-1.0*worksetEdgeSigns(cellCounter,4);
      }
      if (!faceIsOwned[elemToFace(cell,1)]) {
        worksetEdgeSigns(cellCounter,1)=-1.0*worksetEdgeSigns(cellCounter,1);
        worksetEdgeSigns(cellCounter,5)=-1.0*worksetEdgeSigns(cellCounter,5);
      }
      if (!faceIsOwned[elemToFace(cell,2)]) {
        worksetEdgeSigns(cellCounter,2)=-1.0*worksetEdgeSigns(cellCounter,2);
        worksetEdgeSigns(cellCounter,6)=-1.0*worksetEdgeSigns(cellCounter,6);
      }
      if (!faceIsOwned[elemToFace(cell,3)]) {
        worksetEdgeSigns(cellCounter,3)=-1.0*worksetEdgeSigns(cellCounter,3);
        worksetEdgeSigns(cellCounter,7)=-1.0*worksetEdgeSigns(cellCounter,7);
      }

      if (dump){
        for (int iedge=0; iedge<numEdgesPerElem; iedge++) {
          edgeSign[iedge][cell] = worksetEdgeSigns(cellCounter,iedge);
        }
      }

      cellCounter++;

    } // end loop over workset cells

    /**********************************************************************************/
    /*                                Allocate arrays                                 */
    /**********************************************************************************/

    // Containers for Jacobian
    FieldContainer<double> worksetJacobian (worksetSize, numCubPoints, spaceDim, spaceDim);
    FieldContainer<double> worksetJacobInv (worksetSize, numCubPoints, spaceDim, spaceDim);
    FieldContainer<double> worksetJacobDet (worksetSize, numCubPoints);

    // Container for cubature points in physical space
    FieldContainer<double> worksetCubPoints (worksetSize,numCubPoints, cubDim);

    // Containers for element HGRAD mass matrix
    FieldContainer<double> massMatrixHGrad           (worksetSize, numFieldsG, numFieldsG);
    FieldContainer<double> weightedMeasure           (worksetSize, numCubPoints);
    FieldContainer<double> weightedMeasureMuInv      (worksetSize, numCubPoints);
    FieldContainer<double> HGValsTransformed         (worksetSize, numFieldsG, numCubPoints);
    FieldContainer<double> HGValsTransformedWeighted (worksetSize, numFieldsG, numCubPoints);

    // Containers for element HCURL mass matrix
    FieldContainer<double> massMatrixHCurl           (worksetSize, numFieldsC, numFieldsC);
    FieldContainer<double> massMatrixHCurlNoSigma    (worksetSize, numFieldsC, numFieldsC);
    FieldContainer<double> weightedMeasureSigma      (worksetSize, numCubPoints);
    FieldContainer<double> HCValsTransformed         (worksetSize, numFieldsC, numCubPoints, spaceDim);
    FieldContainer<double> HCValsTransformedWeighted (worksetSize, numFieldsC, numCubPoints, spaceDim);
    FieldContainer<double> HCValsTransformedWeightedNoSigma (worksetSize, numFieldsC, numCubPoints, spaceDim);

    // Containers for element HCURL stiffness matrix
    FieldContainer<double> stiffMatrixHCurl          (worksetSize, numFieldsC, numFieldsC);
    FieldContainer<double> weightedMeasureMu         (worksetSize, numCubPoints);
    FieldContainer<double> HCurlsTransformed         (worksetSize, numFieldsC, numCubPoints, spaceDim);
    FieldContainer<double> HCurlsTransformedWeighted (worksetSize, numFieldsC, numCubPoints, spaceDim);

    // Containers for right hand side vectors
    FieldContainer<double> rhsDatag           (worksetSize, numCubPoints, cubDim);
    FieldContainer<double> rhsDatah           (worksetSize, numCubPoints, cubDim);
    FieldContainer<double> gC                 (worksetSize, numFieldsC);
    FieldContainer<double> hC                 (worksetSize, numFieldsC);

    // Containers for right hand side boundary term
    FieldContainer<double> hCBoundary         (1, numFieldsC);
    FieldContainer<double> refFacePoints      (numFacePoints,spaceDim);
    FieldContainer<double> cellNodes          (1, numNodesPerElem, spaceDim);
    FieldContainer<double> worksetFacePoints  (1, numFacePoints, spaceDim);
    FieldContainer<double> faceJacobians      (1, numFacePoints, spaceDim, spaceDim);
    FieldContainer<double> faceJacobInv       (1, numFacePoints, spaceDim, spaceDim);
    FieldContainer<double> faceNormal         (1, numFacePoints, spaceDim);
    FieldContainer<double> bcFaceCVals        (numFieldsC, numFacePoints, spaceDim);
    FieldContainer<double> faceVFieldVals     (1, numFacePoints, spaceDim);
    FieldContainer<double> bcCValsTransformed (1, numFieldsC, numFacePoints, spaceDim);
    FieldContainer<double> bcFieldDotNormal   (1, numFieldsC, numFacePoints);
    FieldContainer<double> bcEdgeSigns        (1, numFieldsC);


    /**********************************************************************************/
    /*                                Calculate Jacobians                             */
    /**********************************************************************************/

    IntrepidCTools::setJacobian   (worksetJacobian, cubPoints, cellWorkset, cellType);
    IntrepidCTools::setJacobianInv(worksetJacobInv, worksetJacobian );
    IntrepidCTools::setJacobianDet(worksetJacobDet, worksetJacobian );

    if(MyPID==0) {std::cout << "Calculate Jacobians                         "
                            << Time.ElapsedTime() << " sec \n"; Time.ResetStartTime();}


    /**********************************************************************************/
    /*                          Compute HGRAD Mass Matrix                             */
    /**********************************************************************************/

    // transform to physical coordinates
    IntrepidFSTools::HGRADtransformVALUE<double>(HGValsTransformed, HGVals);

    // compute weighted measure
    IntrepidFSTools::computeCellMeasure<double>(weightedMeasure, worksetJacobDet, cubWeights);

    // combine mu value with weighted measure
    cellCounter = 0;
    for(int cell = worksetBegin; cell < worksetEnd; cell++){
      for (int nPt = 0; nPt < numCubPoints; nPt++){
        weightedMeasureMuInv(cellCounter,nPt) = weightedMeasure(cellCounter,nPt) / muVal(cell);
      }
      cellCounter++;
    }

    // multiply values with weighted measure
    IntrepidFSTools::multiplyMeasure<double>(HGValsTransformedWeighted,
                                             weightedMeasureMuInv, HGValsTransformed);

    // integrate to compute element mass matrix
    IntrepidFSTools::integrate<double>(massMatrixHGrad,
                                       HGValsTransformed, HGValsTransformedWeighted, COMP_BLAS);

    if(MyPID==0) {std::cout << "Compute HGRAD Mass Matrix                   "
                            << Time.ElapsedTime() << " sec \n"; Time.ResetStartTime();}

#ifdef HAVE_XPETRA_EPETRA
    // Statistics: Phase 2a
    MLStatistics.Phase2a(worksetJacobDet,cubWeights);
#endif

    /**********************************************************************************/
    /*                          Compute HCURL Mass Matrix                             */
    /**********************************************************************************/

    // transform to physical coordinates
    IntrepidFSTools::HCURLtransformVALUE<double>(HCValsTransformed, worksetJacobInv,
                                                 HCVals);

    // combine sigma value with weighted measure
    cellCounter = 0;
    for(int cell = worksetBegin; cell < worksetEnd; cell++){
      for (int nPt = 0; nPt < numCubPoints; nPt++){
        weightedMeasureSigma(cellCounter,nPt) = weightedMeasure(cellCounter,nPt) * sigmaVal(cell);
      }
      cellCounter++;
    }

    // multiply by weighted measure
    IntrepidFSTools::multiplyMeasure<double>(HCValsTransformedWeighted,
                                             weightedMeasureSigma, HCValsTransformed);

    // integrate to compute element mass matrix
    IntrepidFSTools::integrate<double>(massMatrixHCurl,
                                       HCValsTransformed, HCValsTransformedWeighted,
                                       COMP_BLAS);

    // apply edge signs
    IntrepidFSTools::applyLeftFieldSigns<double> (massMatrixHCurl, worksetEdgeSigns);
    IntrepidFSTools::applyRightFieldSigns<double>(massMatrixHCurl, worksetEdgeSigns);


    // Now for the no-sigma version
    IntrepidFSTools::multiplyMeasure<double>(HCValsTransformedWeightedNoSigma,
                                             weightedMeasure, HCValsTransformed);

    // integrate to compute element mass matrix
    IntrepidFSTools::integrate<double>(massMatrixHCurlNoSigma,
                                       HCValsTransformed, HCValsTransformedWeightedNoSigma,
                                       COMP_BLAS);

    if(MyPID==0) {std::cout << "Compute HCURL Mass Matrix                   "
                            << Time.ElapsedTime() << " sec \n"; Time.ResetStartTime();}


    /**********************************************************************************/
    /*                     Compute HCURL Stiffness Matrix                             */
    /**********************************************************************************/

    // transform to physical coordinates
    IntrepidFSTools::HCURLtransformCURL<double>(HCurlsTransformed, worksetJacobian, worksetJacobDet,
                                                HCurls);

    // combine mu value with weighted measure
    cellCounter = 0;
    for(int cell = worksetBegin; cell < worksetEnd; cell++){
      for (int nPt = 0; nPt < numCubPoints; nPt++){
        weightedMeasureMu(cellCounter,nPt) = weightedMeasure(cellCounter,nPt) * muVal(cell);
      }
      cellCounter++;
    }

    // multiply by weighted measure
    IntrepidFSTools::multiplyMeasure<double>(HCurlsTransformedWeighted,
                                             weightedMeasureMuInv, HCurlsTransformed);

    // integrate to compute element stiffness matrix
    IntrepidFSTools::integrate<double>(stiffMatrixHCurl,
                                       HCurlsTransformed, HCurlsTransformedWeighted,
                                       COMP_BLAS);

    // apply edge signs
    IntrepidFSTools::applyLeftFieldSigns<double> (stiffMatrixHCurl, worksetEdgeSigns);
    IntrepidFSTools::applyRightFieldSigns<double>(stiffMatrixHCurl, worksetEdgeSigns);

    if(MyPID==0) {std::cout << "Compute HCURL Stiffness Matrix              "
                            << Time.ElapsedTime() << " sec \n"; Time.ResetStartTime();}


    /**********************************************************************************/
    /*                          Build Right Hand Side                                 */
    /**********************************************************************************/

    // transform integration points to physical points
    IntrepidCTools::mapToPhysicalFrame(worksetCubPoints, cubPoints, cellWorkset, cellType);

    // evaluate right hand side functions at physical points
    for(int cell = worksetBegin; cell < worksetEnd; cell++){

      // Compute cell ordinal relative to the current workset
      int worksetCellOrdinal = cell - worksetBegin;

      for (int nPt = 0; nPt < numCubPoints; nPt++){
        double x = worksetCubPoints(worksetCellOrdinal,nPt,0);
        double y = worksetCubPoints(worksetCellOrdinal,nPt,1);
        double z = worksetCubPoints(worksetCellOrdinal,nPt,2);
        double du1, du2, du3;

        evalCurlCurlu(du1, du2, du3, x, y, z, muVal(cell));
        rhsDatag(worksetCellOrdinal,nPt,0) = du1;
        rhsDatag(worksetCellOrdinal,nPt,1) = du2;
        rhsDatag(worksetCellOrdinal,nPt,2) = du3;


        double u1, u2, u3;
        evalu(u1, u2, u3, x, y, z);
        rhsDatah(worksetCellOrdinal,nPt,0) = u1;
        rhsDatah(worksetCellOrdinal,nPt,1) = u2;
        rhsDatah(worksetCellOrdinal,nPt,2) = u3;

      }
    }

    // integrate (g,curl w) term
    //      IntrepidFSTools::integrate<double>(gC, rhsDatag, HCurlsTransformedWeighted,
    //                             COMP_BLAS);

    // integrate (h,w) term
    IntrepidFSTools::integrate<double>(gC, rhsDatag, HCValsTransformedWeighted, COMP_BLAS);

    IntrepidFSTools::integrate<double>(hC, rhsDatah, HCValsTransformedWeighted, COMP_BLAS);


    // apply signs
    IntrepidFSTools::applyFieldSigns<double>(gC, worksetEdgeSigns);
    IntrepidFSTools::applyFieldSigns<double>(hC, worksetEdgeSigns);


    // evaluate RHS boundary term
    for(int cell = worksetBegin; cell < worksetEnd; cell++){

      // Compute cell ordinal relative to the current workset
      int worksetCellOrdinal = cell - worksetBegin;

      // evaluate boundary term
      for (int iface = 0; iface < numFacesPerElem; iface++){
        if (faceOnBoundary(elemToFace(cell,iface))){

          // cell nodal coordinates
          for (int inode =0; inode < numNodesPerElem; inode++){
            cellNodes(0,inode,0) = cellWorkset(worksetCellOrdinal,inode,0);
            cellNodes(0,inode,1) = cellWorkset(worksetCellOrdinal,inode,1);
            cellNodes(0,inode,2) = cellWorkset(worksetCellOrdinal,inode,2);
          }

          // cell edge signs
          for (int iedge =0; iedge < numEdgesPerElem; iedge++){
            bcEdgeSigns(0,iedge) = worksetEdgeSigns(worksetCellOrdinal,iedge);
          }

          // map Gauss points on quad to reference face: paramFacePoints -> refFacePoints
          IntrepidCTools::mapToReferenceSubcell(refFacePoints,
                                                paramFacePoints,
                                                2, iface, cellType);

          // get basis values at points on reference cell
          hexHCurlBasis.getValues(bcFaceCVals, refFacePoints, OPERATOR_VALUE);

          // compute Jacobians at Gauss pts. on reference face for all parent cells
          IntrepidCTools::setJacobian(faceJacobians,
                                      refFacePoints,
                                      cellNodes, cellType);
          IntrepidCTools::setJacobianInv(faceJacobInv, faceJacobians );

          // transform to physical coordinates
          IntrepidFSTools::HCURLtransformVALUE<double>(bcCValsTransformed, faceJacobInv,
                                                       bcFaceCVals);

          // map Gauss points on quad from ref. face to face workset: refFacePoints -> worksetFacePoints
          IntrepidCTools::mapToPhysicalFrame(worksetFacePoints,
                                             refFacePoints,
                                             cellNodes, cellType);

          // Compute face normals
          IntrepidCTools::getPhysicalFaceNormals(faceNormal,
                                                 faceJacobians,
                                                 iface, cellType);

          // compute the dot product and multiply by Gauss weights
          for (int nF = 0; nF < numFieldsC; nF++){
            for(int nPt = 0; nPt < numFacePoints; nPt++){
              bcFieldDotNormal(0,nF,nPt)=0.0;
              for (int dim = 0; dim < spaceDim; dim++){
                bcFieldDotNormal(0,nF,nPt) += bcCValsTransformed(0,nF,nPt,dim)
                  * faceNormal(0,nPt,dim) * paramFaceWeights(nPt);
              } //dim
            } //nPt
          } //nF

            // integrate
          //               IntrepidFSTools::integrate<double>(hCBoundary, divuFace, bcFieldDotNormal,
          //                             COMP_CPP);

          // apply signs
          //              IntrepidFSTools::applyFieldSigns<double>(hCBoundary, bcEdgeSigns);

          // add into hC term
          //              for (int nF = 0; nF < numFieldsC; nF++){
          //                  hC(worksetCellOrdinal,nF) = hC(worksetCellOrdinal,nF) - hCBoundary(0,nF);
          //              }

        } // if faceOnBoundary

      } // numFaces

    }// *** workset cell loop **


    if(MyPID==0) {std::cout << "Compute right-hand side                     "
                            << Time.ElapsedTime() << " sec \n"; Time.ResetStartTime();}


    /**********************************************************************************/
    /*                         Assemble into Global Matrix                            */
    /**********************************************************************************/

    // Loop over workset cells
    for(int cell = worksetBegin; cell < worksetEnd; cell++){

      // Compute cell ordinal relative to the current workset
      int worksetCellOrdinal = cell - worksetBegin;


      /*** Assemble H(grad) mass matrix ***/

      // loop over nodes for matrix row
      for (int cellNodeRow = 0; cellNodeRow < numFieldsG; cellNodeRow++){

        int localNodeRow  = elemToNode(cell, cellNodeRow);
        int globalNodeRow = globalNodeIds[localNodeRow];

        // loop over nodes for matrix column
        for (int cellNodeCol = 0; cellNodeCol < numFieldsG; cellNodeCol++){

          int localNodeCol  = elemToNode(cell, cellNodeCol);
          int globalNodeCol = globalNodeIds[localNodeCol];
          double massGContribution = massMatrixHGrad(worksetCellOrdinal, cellNodeRow, cellNodeCol);

          MassMatrixG.InsertGlobalValues(1, &globalNodeRow, 1, &globalNodeCol, &massGContribution);

        }// *** cell node col loop ***
      }// *** cell node row loop ***


      /*** Assemble H(curl) mass matrix, stiffness matrix and right-hand side ***/

      // loop over edges for matrix row
      for (int cellEdgeRow = 0; cellEdgeRow < numFieldsC; cellEdgeRow++){

        int localEdgeRow  = elemToEdge(cell, cellEdgeRow);
        int globalEdgeRow = globalEdgeIds[localEdgeRow];
        double rhsContribution = gC(worksetCellOrdinal, cellEdgeRow) + hC(worksetCellOrdinal, cellEdgeRow);

        rhsVector.SumIntoGlobalValues(1, &globalEdgeRow, &rhsContribution);

        // loop over edges for matrix column
        for (int cellEdgeCol = 0; cellEdgeCol < numFieldsC; cellEdgeCol++){

          int localEdgeCol  = elemToEdge(cell, cellEdgeCol);
          int globalEdgeCol = globalEdgeIds[localEdgeCol];

          double massCContribution  = massMatrixHCurl (worksetCellOrdinal, cellEdgeRow, cellEdgeCol);
          double stiffCContribution = stiffMatrixHCurl(worksetCellOrdinal, cellEdgeRow, cellEdgeCol);

          double massCContribution1  = massMatrixHCurlNoSigma (worksetCellOrdinal, cellEdgeRow, cellEdgeCol);

          MassMatrixC.InsertGlobalValues (1, &globalEdgeRow, 1, &globalEdgeCol, &massCContribution);
          MassMatrixC1.InsertGlobalValues (1, &globalEdgeRow, 1, &globalEdgeCol, &massCContribution1);

          StiffMatrixC.InsertGlobalValues(1, &globalEdgeRow, 1, &globalEdgeCol, &stiffCContribution);


        }// *** cell edge col loop ***
      }// *** cell edge row loop ***

    }// *** workset cell loop **
  }// *** workset loop ***

  if(MyPID==0) {std::cout << "Assemble Matrices                           "
                          << Time.ElapsedTime() << " sec \n"; Time.ResetStartTime();}


  /**********************************************************************************/
  /********************* ASSEMBLE OVER MULTIPLE PROCESSORS **************************/
  /**********************************************************************************/

  // Assemble over multiple processors, if necessary
  MassMatrixG.GlobalAssemble();  MassMatrixG.FillComplete();
  StiffMatrixC.GlobalAssemble(); StiffMatrixC.FillComplete();
  MassMatrixC.GlobalAssemble();  MassMatrixC.FillComplete();
  MassMatrixC1.GlobalAssemble(); MassMatrixC1.FillComplete();
  rhsVector.GlobalAssemble();

#ifdef HAVE_XPETRA_EPETRA
  MLStatistics.Phase2b(Teuchos::rcp(&MassMatrixG.Graph(),false),Teuchos::rcp(&nCoord,false));
#endif

  if(MyPID==0) {std::cout << "Global assembly                             "
                          << Time.ElapsedTime() << " sec \n"; Time.ResetStartTime();}

  if (dump) {
    // Node Coordinates
    EpetraExt::VectorToMatrixMarketFile("coordx.dat",Nx,0,0,false);
    EpetraExt::VectorToMatrixMarketFile("coordy.dat",Ny,0,0,false);
    EpetraExt::VectorToMatrixMarketFile("coordz.dat",Nz,0,0,false);

    // Edge Coordinates
    EpetraExt::VectorToMatrixMarketFile("ecoordx.dat",EDGE_X);
    EpetraExt::VectorToMatrixMarketFile("ecoordy.dat",EDGE_Y);
    EpetraExt::VectorToMatrixMarketFile("ecoordz.dat",EDGE_Z);

    // Boundary Application
    EpetraExt::VectorToMatrixMarketFile("boundary_v.dat",v);

    // Edge signs
    EpetraExt::MultiVectorToMatrixMarketFile("edge_signs.dat",edgeSign,0,0,false);

    EpetraExt::RowMatrixToMatrixMarketFile("mag_k1_matrix.mat",StiffMatrixC);
    EpetraExt::RowMatrixToMatrixMarketFile("mag_ms_matrix.mat",MassMatrixC);
    EpetraExt::RowMatrixToMatrixMarketFile("mag_m1_matrix.mat",MassMatrixC1);
    EpetraExt::RowMatrixToMatrixMarketFile("mag_t_matrix.mat",DGrad);
  }
  

  /**********************************************************************************/
  /*********************** ADJUST MATRICES AND RHS FOR BCs **************************/
  /**********************************************************************************/

  // Build the inverse diagonal for MassMatrixG
  Epetra_Vector DiagG(MassMatrixG.RowMap());
  DiagG.PutScalar(1.0);
  MassMatrixG.Multiply(false,DiagG,DiagG);
  for(int i=0;i<DiagG.MyLength();i++) {
    DiagG[i]=1.0/DiagG[i];
  }
  Epetra_CrsMatrix MassMatrixGinv(Copy,MassMatrixG.RowMap(),MassMatrixG.RowMap(),1);
  for(int i=0;i<DiagG.MyLength();i++) {
    int CID=MassMatrixG.GCID(i);
    MassMatrixGinv.InsertGlobalValues(MassMatrixG.GRID(i),1,&(DiagG[i]),&CID);
  }
  MassMatrixGinv.FillComplete();

  // Zero out entries that correspond to boundary nodes
  for(int i=0;i<numNodes;i++) {
    if (nodeOnBoundary(i)){
      double val=0.0;
      int index = globalNodeIds[i];
      MassMatrixGinv.ReplaceGlobalValues(index,1,&val,&index);
    }
  }

  // Get the full matrix operator Kc += Mc
  int rv=EpetraExt::MatrixMatrix::Add(MassMatrixC,false,scaling,StiffMatrixC,1.0);
  if(rv!=0) {printf("EpetraExt::MatrixMatrix:Add FAILED\n"); exit(1);}


  // Apply it to v
  Epetra_MultiVector rhsDir(globalMapC,true);
  StiffMatrixC.Apply(v,rhsDir);

  // Update right-hand side
  rhsVector.Update(-1.0,rhsDir,1.0);

  // Adjust rhs due to Dirichlet boundary conditions
  indbc=0;
  int indOwned=0;
  for (int i=0; i<numEdges; i++){
    if (edgeIsOwned[i]){
      if (edgeOnBoundary(i)){
        indbc++;
        rhsVector[0][indOwned]=bndyEdgeVal(bndyEdgeToEdge(i));
        rhsDir[0][indOwned]=0.0;
      }
      indOwned++;
    }
  }

  if (dump) {
    EpetraExt::RowMatrixToMatrixMarketFile("edge_matrix_nobcs.mat",StiffMatrixC);
  }

  // Zero out rows and columns of stiffness matrix corresponding to Dirichlet edges
  //  and add one to diagonal.
  ML_Epetra::Apply_OAZToMatrix(BCEdges, numBCEdges, StiffMatrixC);

  delete [] BCEdges;

  if(MyPID==0) {std::cout << "Adjust global matrix and rhs due to BCs     " << Time.ElapsedTime()
                          << " sec \n"; Time.ResetStartTime();}

  if (dump) {
    EpetraExt::RowMatrixToMatrixMarketFile("mag_m0_matrix.mat",MassMatrixG);
    EpetraExt::RowMatrixToMatrixMarketFile("edge_matrix.mat",StiffMatrixC);
  }

  /**********************************************************************************/
  /*********************************** SOLVE ****************************************/
  /**********************************************************************************/

#ifdef HAVE_XPETRA_EPETRA
  MLStatistics.Phase3();
  Teuchos::ParameterList problemStatistics = MLStatistics.GetStatistics();
  if(MyPID==0) {
    std::cout<<"*** Problem Statistics ***"<<std::endl;
    std::cout<<problemStatistics<<std::endl;
  }
#endif

  double TotalErrorResidual=0, TotalErrorExactSol=0;

  // Parameter list for ML
  Teuchos::ParameterList MLList;
  if(inputList.isSublist("ML")) {
    MLList = inputList.sublist("ML");
  } else {
    ML_Epetra::SetDefaultsRefMaxwell(MLList);
    Teuchos::ParameterList &List11=MLList.sublist("refmaxwell: 11list");
    Teuchos::ParameterList &List11c=List11.sublist("edge matrix free: coarse");
    Teuchos::ParameterList &List22=MLList.sublist("refmaxwell: 22list");
    // MLList.set("refmaxwell: mode","none");
    MLList.set("refmaxwell: mode","additive");
    MLList.set("refmaxwell: disable addon",false);
    MLList.set("aggregation: type","Uncoupled-MIS");
    MLList.set("ML output",10);
    MLList.set("smoother: type","Chebyshev");
    // MLList.set("print hierarchy", 0);

    List11.set("ML output",10);
    // List11.set("print hierarchy", true);
    List22.set("coarse: type","Amesos-KLU");
    List22.set("ML output",10);

    List11c.set("coarse: type","Amesos-KLU");
    List11c.set("ML output",10);
    List11c.set("PDE equations",3);
  }
  Teuchos::ParameterList &List11=MLList.sublist("refmaxwell: 11list");
  List11.set("x-coordinates",&Nx[0]);
  List11.set("y-coordinates",&Ny[0]);
  List11.set("z-coordinates",&Nz[0]);

  Teuchos::ParameterList &List22=MLList.sublist("refmaxwell: 22list");
  List22.set("x-coordinates",&Nx[0]);
  List22.set("y-coordinates",&Ny[0]);
  List22.set("z-coordinates",&Nz[0]);

  // Parameter list for MueLu
  Teuchos::ParameterList MueLuList;
  Teuchos::RCP<const Teuchos::Comm<int> > comm = Teuchos::DefaultComm<int>::getComm();

  if(inputList.isSublist("MueLu")) {
    MueLuList = inputList.sublist("MueLu");
  } else {
    Teuchos::ParameterList &MueList11=MueLuList.sublist("refmaxwell: 11list");
    Teuchos::ParameterList &MueList22=MueLuList.sublist("refmaxwell: 22list");
    MueLuList.set("aggregation: type","Uncoupled");
    // MueLuList.set("refmaxwell: mode","none");
    MueLuList.set("refmaxwell: mode","additive");
    MueLuList.set("refmaxwell: disable addon",false);;
    MueLuList.set("smoother: type","CHEBYSHEV");

    Teuchos::ParameterList &MueLuSmootherList=MueLuList.sublist("smoother: params");
    MueLuSmootherList.set("chebyshev: degree",2);
    MueLuSmootherList.set("chebyshev: ratio eigenvalue",30.0);

    MueList11.set("coarse: max size", 128);
    MueList11.set("smoother: type", "CHEBYSHEV");
    //MueList11.sublist("smoother: params").set("chebyshev: max eigenvalue",1.85123);
    MueList11.sublist("smoother: params").set("chebyshev: ratio eigenvalue",30.0);
    MueList11.sublist("smoother: params").set("chebyshev: degree",2);
    //MueList11.set("coarse: type","Amesos-KLU");
    MueList11.set("number of equations",3);

    MueList22.set("coarse: max size", 128);
    MueList22.set("smoother: type", "CHEBYSHEV");
    //MueList22.sublist("smoother: params").set("chebyshev: max eigenvalue",1.431);
    MueList22.sublist("smoother: params").set("chebyshev: ratio eigenvalue",30.0);
    MueList22.sublist("smoother: params").set("chebyshev: degree",2);
    //MueList22.set("coarse: type","Amesos-KLU");
  }

  
#if defined(HAVE_TRILINOSCOUPLINGS_AVATAR) && defined(HAVE_TRILINOSCOUPLINGS_MUELU) && defined(HAVE_XPETRA_EPETRA)
  Teuchos::ParameterList &MueList11=MueLuList.sublist("refmaxwell: 11list");
  Teuchos::ParameterList &MueList22=MueLuList.sublist("refmaxwell: 22list");
 
  std::vector<std::string> AvatarSublists{"Avatar-MueLu-Fine","Avatar-MueLu-11","Avatar-MueLu-22"};
  std::vector<Teuchos::ParameterList *> MueLuSublists{&MueLuList,&MueList11,&MueList22};
  for (int i=0; i<(int)AvatarSublists.size(); i++) {
    if (inputList.isSublist(AvatarSublists[i])) {
      Teuchos::ParameterList problemFeatures = problemStatistics;
      Teuchos::ParameterList avatarParams = inputList.sublist(AvatarSublists[i]);
      std::cout<<"*** Avatar["<<AvatarSublists[i]<<"] Parameters ***\n"<<avatarParams<<std::endl;
      
      MueLu::AvatarInterface avatar(comm,avatarParams);
      std::cout<<"*** Avatar Setup ***"<<std::endl;
      avatar.Setup();
      avatar.SetMueLuParameters(problemFeatures,*MueLuSublists[i], true);
    }
  }
#endif

  Epetra_FEVector xh(rhsVector);

  MassMatrixC.SetLabel("Ms");
  MassMatrixC1.SetLabel("M1");
  StiffMatrixC.SetLabel("K1");
  DGrad.SetLabel("D0");
  MassMatrixGinv.SetLabel("M0^{-1}");

  char probType[12] = "maxwell";


  // Form the Stiffness+Mass Matrix
  StiffMatrixC.SetLabel("CurlCurl+Mass");

  if (solverName == "ML") {
    // ML version
    if(MyPID==0) {std::cout << "\n\nML solve \n";}
    TestMultiLevelPreconditioner_Maxwell(probType,MLList,StiffMatrixC,
                                         DGrad,MassMatrixGinv,MassMatrixC,MassMatrixC1,
                                         xh,rhsVector,
                                         TotalErrorResidual, TotalErrorExactSol);
  }

#ifdef HAVE_TRILINOSCOUPLINGS_STRATIMIKOS
  if (solverName == "ML-Stratimikos") {
    if(MyPID==0) {std::cout << "\n\nML Stratimikos solve \n";}
    TestMultiLevelPreconditioner_Stratimikos(probType,MLList,StiffMatrixC,
                                             DGrad,MassMatrixGinv,MassMatrixC,MassMatrixC1,
                                             xh,rhsVector,
                                             TotalErrorResidual, TotalErrorExactSol);
  }
#endif

  Epetra_MultiVector coords(MassMatrixGinv.RowMap(),3);
  if (solverName == "MueLu" || solverName == "Stratimikos") {
    // build coordinates multivector

    double* xx = coords[0];
    double* yy = coords[1];
    double* zz = coords[2];
    for(int i = 0; i < MassMatrixGinv.RowMap(). NumMyElements(); ++i) {
      xx[i] = Nx[i];
      yy[i] = Ny[i];
      zz[i] = Nz[i];
    }

  }

#if defined(HAVE_MUELU_EPETRA) and defined(HAVE_TRILINOSCOUPLINGS_MUELU)
  if (solverName == "MueLu") {
    // MueLu RefMaxwell
    if(MyPID==0) {std::cout << "\n\nMueLu solve \n";}
    TestMueLuMultiLevelPreconditioner_Maxwell(probType,MueLuList,StiffMatrixC,
                                              DGrad,MassMatrixGinv,MassMatrixC,MassMatrixC1,
                                              coords,
                                              xh,rhsVector,
                                              TotalErrorResidual, TotalErrorExactSol);
  }

  #ifdef HAVE_TRILINOSCOUPLINGS_STRATIMIKOS
  if (solverName == "Stratimikos") {
    if(MyPID==0) {std::cout << "\n\nStratimikos solve \n";}
    Teuchos::ParameterList SList;
    Teuchos::updateParametersFromXmlFile(xmlStratimikos,Teuchos::ptr (&SList));
    TestPreconditioner_Stratimikos(probType,SList,StiffMatrixC,
                                   DGrad,MassMatrixGinv,MassMatrixC,MassMatrixC1,
                                   coords,Nx,Ny,Nz,
                                   xh,rhsVector,
                                   TotalErrorResidual, TotalErrorExactSol);
  }
  #endif
#endif


  /**********************************************************************************/
  /**************************** CALCULATE ERROR *************************************/
  /**********************************************************************************/

  if (MyPID == 0) {Time.ResetStartTime();}

  double L2err = 0.0;
  double HCurlerr = 0.0;
  double Linferr = 0.0;
  double L2errTot = 0.0;
  double HCurlerrTot = 0.0;
  double LinferrTot = 0.0;

#ifdef HAVE_MPI
  // Import solution onto current processor
  Epetra_Map  solnMap(numEdgesGlobal, numEdgesGlobal, 0, Comm);
  Epetra_Import  solnImporter(solnMap, globalMapC);
  Epetra_FEVector  uCoeff(solnMap);
  uCoeff.Import(xh, solnImporter, Insert);
#endif

  int numCells = 1;
  FieldContainer<double> hexEdgeSigns(numCells, numFieldsC);
  FieldContainer<double> hexNodes(numCells, numFieldsG, spaceDim);

  // Get cubature points and weights for error calc (may be different from previous)
  DefaultCubatureFactory<double>  cubFactoryErr;
  int cubDegErr = 3;
  Teuchos::RCP<Cubature<double> > hexCubErr = cubFactoryErr.create(cellType, cubDegErr);
  int cubDimErr       = hexCubErr->getDimension();
  int numCubPointsErr = hexCubErr->getNumPoints();
  FieldContainer<double> cubPointsErr(numCubPointsErr, cubDimErr);
  FieldContainer<double> cubWeightsErr(numCubPointsErr);
  hexCubErr->getCubature(cubPointsErr, cubWeightsErr);
  FieldContainer<double> physCubPointsE(numCells,numCubPointsErr, cubDimErr);

  // Containers for Jacobian
  FieldContainer<double> hexJacobianE(numCells, numCubPointsErr, spaceDim, spaceDim);
  FieldContainer<double> hexJacobInvE(numCells, numCubPointsErr, spaceDim, spaceDim);
  FieldContainer<double> hexJacobDetE(numCells, numCubPointsErr);
  FieldContainer<double> weightedMeasureE(numCells, numCubPointsErr);

  // Evaluate basis values and curls at cubature points
  FieldContainer<double> uhCVals(numFieldsC, numCubPointsErr, spaceDim);
  FieldContainer<double> uhCValsTrans(numCells,numFieldsC, numCubPointsErr, spaceDim);
  FieldContainer<double> uhCurls(numFieldsC, numCubPointsErr, spaceDim);
  FieldContainer<double> uhCurlsTrans(numCells, numFieldsC, numCubPointsErr, spaceDim);
  hexHCurlBasis.getValues(uhCVals, cubPointsErr, OPERATOR_VALUE);
  hexHCurlBasis.getValues(uhCurls, cubPointsErr, OPERATOR_CURL);

  // Loop over elements
  for (int k=0; k<numElems; k++){

    double L2errElem = 0.0;
    double HCurlerrElem = 0.0;
    double uExact1, uExact2, uExact3;
    double curluExact1, curluExact2, curluExact3;

    // physical cell coordinates
    for (int i=0; i<numNodesPerElem; i++) {
      hexNodes(0,i,0) = nodeCoord(elemToNode(k,i),0);
      hexNodes(0,i,1) = nodeCoord(elemToNode(k,i),1);
      hexNodes(0,i,2) = nodeCoord(elemToNode(k,i),2);
    }
    // Edge signs
    for (int j=0; j<numEdgesPerElem; j++) {
      if (elemToNode(k,refEdgeToNode(j,0))==edgeToNode(elemToEdge(k,j),0) &&
          elemToNode(k,refEdgeToNode(j,1))==edgeToNode(elemToEdge(k,j),1))
        hexEdgeSigns(0,j) = 1.0;
      else
        hexEdgeSigns(0,j) = -1.0;
    }

    // modify signs for edges whose signs were defined on another processor
    if (!faceIsOwned[elemToFace(k,0)]) {
      hexEdgeSigns(0,0)=-1.0*hexEdgeSigns(0,0);
      hexEdgeSigns(0,4)=-1.0*hexEdgeSigns(0,4);
    }
    if (!faceIsOwned[elemToFace(k,1)]) {
      hexEdgeSigns(0,1)=-1.0*hexEdgeSigns(0,1);
      hexEdgeSigns(0,5)=-1.0*hexEdgeSigns(0,5);
    }
    if (!faceIsOwned[elemToFace(k,2)]) {
      hexEdgeSigns(0,2)=-1.0*hexEdgeSigns(0,2);
      hexEdgeSigns(0,6)=-1.0*hexEdgeSigns(0,6);
    }
    if (!faceIsOwned[elemToFace(k,3)]) {
      hexEdgeSigns(0,3)=-1.0*hexEdgeSigns(0,3);
      hexEdgeSigns(0,7)=-1.0*hexEdgeSigns(0,7);
    }

    // compute cell Jacobians, their inverses and their determinants
    IntrepidCTools::setJacobian(hexJacobianE, cubPointsErr, hexNodes, cellType);
    IntrepidCTools::setJacobianInv(hexJacobInvE, hexJacobianE );
    IntrepidCTools::setJacobianDet(hexJacobDetE, hexJacobianE );

    // transform integration points to physical points
    IntrepidCTools::mapToPhysicalFrame(physCubPointsE, cubPointsErr, hexNodes, cellType);

    // transform basis values to physical coordinates
    IntrepidFSTools::HCURLtransformVALUE<double>(uhCValsTrans, hexJacobInvE, uhCVals);
    IntrepidFSTools::HCURLtransformCURL<double>(uhCurlsTrans, hexJacobianE, hexJacobDetE, uhCurls);

    // compute weighted measure
    IntrepidFSTools::computeCellMeasure<double>(weightedMeasureE, hexJacobDetE, cubWeightsErr);

    // loop over cubature points
    for (int nPt = 0; nPt < numCubPointsErr; nPt++){

      // get exact solution and curls
      double x = physCubPointsE(0,nPt,0);
      double y = physCubPointsE(0,nPt,1);
      double z = physCubPointsE(0,nPt,2);
      evalu(uExact1, uExact2, uExact3, x, y, z);
      double mu = 1.0; // use mu=1 to get the curl without material parameter
      evalCurlu(curluExact1, curluExact2, curluExact3, x, y, z, mu);

      // calculate approximate solution and curls
      double uApprox1 = 0.0;
      double uApprox2 = 0.0;
      double uApprox3 = 0.0;
      double curluApprox1 = 0.0;
      double curluApprox2= 0.0;
      double curluApprox3 = 0.0;
      for (int i = 0; i < numFieldsC; i++){
        int rowIndex = globalEdgeIds[elemToEdge(k,i)];
#ifdef HAVE_MPI
        double uh1 = uCoeff.Values()[rowIndex];
#else
        double uh1 = xh.Values()[rowIndex];
#endif
        uApprox1 += uh1*uhCValsTrans(0,i,nPt,0)*hexEdgeSigns(0,i);
        uApprox2 += uh1*uhCValsTrans(0,i,nPt,1)*hexEdgeSigns(0,i);
        uApprox3 += uh1*uhCValsTrans(0,i,nPt,2)*hexEdgeSigns(0,i);
        curluApprox1 += uh1*uhCurlsTrans(0,i,nPt,0)*hexEdgeSigns(0,i);
        curluApprox2 += uh1*uhCurlsTrans(0,i,nPt,1)*hexEdgeSigns(0,i);
        curluApprox3 += uh1*uhCurlsTrans(0,i,nPt,2)*hexEdgeSigns(0,i);
      }

      // evaluate the error at cubature points
      Linferr = max(Linferr, abs(uExact1 - uApprox1));
      Linferr = max(Linferr, abs(uExact2 - uApprox2));
      Linferr = max(Linferr, abs(uExact3 - uApprox3));
      L2errElem+=(uExact1 - uApprox1)*(uExact1 - uApprox1)*weightedMeasureE(0,nPt);
      L2errElem+=(uExact2 - uApprox2)*(uExact2 - uApprox2)*weightedMeasureE(0,nPt);
      L2errElem+=(uExact3 - uApprox3)*(uExact3 - uApprox3)*weightedMeasureE(0,nPt);
      HCurlerrElem+=((curluExact1 - curluApprox1)*(curluExact1 - curluApprox1))
        *weightedMeasureE(0,nPt);
      HCurlerrElem+=((curluExact2 - curluApprox2)*(curluExact2 - curluApprox2))
        *weightedMeasureE(0,nPt);
      HCurlerrElem+=((curluExact3 - curluApprox3)*(curluExact3 - curluApprox3))
        *weightedMeasureE(0,nPt);
    }

    L2err+=L2errElem;
    HCurlerr+=HCurlerrElem;
  }


#ifdef HAVE_MPI
  // sum over all processors
  Comm.SumAll(&L2err,&L2errTot,1);
  Comm.SumAll(&HCurlerr,&HCurlerrTot,1);
  Comm.MaxAll(&Linferr,&LinferrTot,1);
#else
  L2errTot = L2err;
  HCurlerrTot = HCurlerr;
  LinferrTot = Linferr;
#endif

  if (MyPID == 0) {
    std::cout << "\n" << "L2 Error:  " << sqrt(L2errTot) <<"\n";
    std::cout << "HCurl Error:  " << sqrt(HCurlerrTot) <<"\n";
    std::cout << "LInf Error:  " << LinferrTot <<"\n\n";
  }


  // delete mesh
  Delete_Pamgen_Mesh();

  //clean up
  for(long long b = 0; b < numElemBlk; b++){
    delete [] elmt_node_linkage[b];
    delete [] element_types[b];
  }
  delete [] block_ids;
  delete [] nodes_per_element;
  delete [] element_attributes;
  delete [] element_types;
  delete [] elmt_node_linkage;
  delete [] elements;
  delete [] nodeCoordx;
  delete [] nodeCoordy;
  delete [] nodeCoordz;

  delete [] globalNodeIds;
  delete [] nodeIsOwned;
  delete [] globalEdgeIds;
  delete [] edgeIsOwned;
  delete [] globalFaceIds;
  delete [] faceIsOwned;
  if(num_node_comm_maps > 0){
    delete [] node_comm_proc_ids;
    delete [] node_cmap_node_cnts;
    delete [] node_cmap_ids;
    for(long long i=0;i<num_node_comm_maps;i++){
      delete [] comm_node_ids[i];
      delete [] comm_node_proc_ids[i];
    }

    delete [] comm_node_ids;
    delete [] comm_node_proc_ids;
  }

  // Summarize timings
  Teuchos::TimeMonitor::report (comm.ptr(), std::cout);

  return 0;
}
/**********************************************************************************/
/********************************* END MAIN ***************************************/
/**********************************************************************************/


/*************************************************************************************/
/****************************** Multiply Ones ****************************************/
/*************************************************************************************/
/** \brief Multiplies Ax = y, where all non-zero entries of A are replaced with the value 1.0

    \param  A                [in]    matrix
    \param  x                [in]    vector
    \param  y                [in]    vector
*/
int Multiply_Ones(const Epetra_CrsMatrix &A,const Epetra_Vector &x,Epetra_Vector &y){
  if(!A.Filled())
    EPETRA_CHK_ERR(-1); // Matrix must be filled.

  double* xp = (double*) x.Values();
  double* yp = (double*) y.Values();
  const Epetra_Import* Importer_=A.Importer();
  const Epetra_Export* Exporter_=A.Exporter();
  Epetra_Vector *xcopy=0, *ImportVector_=0, *ExportVector_=0;

  if (&x==&y && Importer_==0 && Exporter_==0) {
    xcopy = new Epetra_Vector(x);
    xp = (double *) xcopy->Values();
  }
  else if (Importer_)
    ImportVector_ = new Epetra_Vector(Importer_->TargetMap());
  else if (Exporter_)
    ExportVector_ = new Epetra_Vector(Exporter_->SourceMap());


  // If we have a non-trivial importer, we must import elements that are permuted or are on other processors
  if(Importer_ != 0) {
    EPETRA_CHK_ERR(ImportVector_->Import(x, *Importer_, Insert));
    xp = (double*) ImportVector_->Values();
  }

  // If we have a non-trivial exporter, we must export elements that are permuted or belong to other processors
  if(Exporter_ != 0)  yp = (double*) ExportVector_->Values();

  // Do actual computation
  for(int i = 0; i < A.NumMyRows(); i++) {
    int NumEntries,*RowIndices;
    A.Graph().ExtractMyRowView(i,NumEntries,RowIndices);
    double sum = 0.0;
    for(int j = 0; j < NumEntries; j++)
      sum += xp[*RowIndices++];
    yp[i] = sum;
  }

  if(Exporter_ != 0) {
    y.PutScalar(0.0); // Make sure target is zero
    EPETRA_CHK_ERR(y.Export(*ExportVector_, *Exporter_, Add)); // Fill y with Values from export vector
  }
  // Handle case of rangemap being a local replicated map
  if (!A.Graph().RangeMap().DistributedGlobal() && A.Comm().NumProc()>1) EPETRA_CHK_ERR(y.Reduce());

  delete xcopy;
  delete ImportVector_;
  delete ExportVector_;


  return(0);
}

/*************************************************************************************/
/**************************** GET ML RESIDUAL ****************************************/
/*************************************************************************************/
/** \brief Compute ML solution residual

    \param  A                  [in]    discrete operator
    \param  lhs                [in]    solution vector
    \param  rhs                [in]    right hand side vector
    \param  Time               [in]    elapsed time for output
    \param  TotalErrorResidual [out]   error residual
    \param  TotalErrorExactSol [out]   error in xh (not an appropriate measure
    for H(curl) basis functions)
*/
void solution_test(string msg, const Epetra_Operator &A,const Epetra_MultiVector &lhs,const Epetra_MultiVector &rhs,const Epetra_MultiVector &xexact,Epetra_Time & Time, double & TotalErrorExactSol, double& TotalErrorResidual){
  // ==================================================== //
  // compute difference between exact solution and ML one //
  // ==================================================== //
  double d = 0.0, d_tot = 0.0;
  for( int i=0 ; i<lhs.Map().NumMyElements() ; ++i )
    d += (lhs[0][i] - xexact[0][i]) * (lhs[0][i] - xexact[0][i]);

  A.Comm().SumAll(&d,&d_tot,1);

  // ================== //
  // compute ||Ax - b|| //
  // ================== //
  double Norm;
  Epetra_Vector Ax(rhs.Map());
  A.Apply(lhs, Ax);
  Ax.Update(1.0, rhs, -1.0);
  Ax.Norm2(&Norm);

  // NOTE: (x_exact - x) does not make sense for H(curl) or H(grad) basis functions
  if (A.Comm().MyPID() == 0) {
    cout << msg << "......Using " << A.Comm().NumProc() << " processes" << endl;
    cout << msg << "......||A x - b||_2 = " << Norm << endl;
    //    cout << msg << "......||x_exact - x||_2 = " << sqrt(d_tot) << endl;
    cout << msg << "......Total Time = " << Time.ElapsedTime() << endl;
  }

  TotalErrorExactSol += sqrt(d_tot);
  TotalErrorResidual += Norm;
}

/*************************************************************************************/
/*************************** MueLu PRECONDITIONER ************************************/
/*************************************************************************************/
void TestMueLuMultiLevelPreconditioner_Maxwell(char ProblemType[],
                                               Teuchos::ParameterList   & MLList,
                                               Epetra_CrsMatrix   & CurlCurl,
                                               Epetra_CrsMatrix   & D0clean,
                                               Epetra_CrsMatrix   & M0inv,
                                               Epetra_CrsMatrix   & Ms,
                                               Epetra_CrsMatrix   & M1,
                                               Epetra_MultiVector & coords,
                                               Epetra_MultiVector & xh,
                                               Epetra_MultiVector & b,
                                               double & TotalErrorResidual,
                                               double & TotalErrorExactSol){
#if defined(HAVE_MUELU_EPETRA) and defined(HAVE_TRILINOSCOUPLINGS_MUELU)
  typedef double Scalar;
  typedef int LocalOrdinal;
  typedef int GlobalOrdinal;
  typedef Xpetra::EpetraNode Node;
  typedef Scalar SC;
  typedef LocalOrdinal LO;
  typedef GlobalOrdinal GO;
  typedef Node NO;
  typedef Xpetra::Matrix<Scalar,LocalOrdinal,GlobalOrdinal,Node> Matrix;
  typedef Xpetra::CrsMatrixWrap<Scalar,LocalOrdinal,GlobalOrdinal,Node> CrsMatrixWrap;
  typedef Xpetra::CrsMatrix<Scalar,LocalOrdinal,GlobalOrdinal,Node> CrsMatrix;
  typedef Xpetra::EpetraCrsMatrixT<GlobalOrdinal,Node> EpetraCrsMatrix;
  typedef Xpetra::MultiVector<Scalar,LocalOrdinal,GlobalOrdinal,Node> MultiVector;
  typedef Xpetra::EpetraMultiVectorT<GlobalOrdinal,Node> EpetraMultiVector;
  

  Epetra_Time SetupTime(CurlCurl.Comm());

  Teuchos::RCP<CrsMatrix> ccMat = Teuchos::rcp(new EpetraCrsMatrix(Teuchos::rcpFromRef(CurlCurl)));
  Teuchos::RCP<CrsMatrixWrap> ccOp = Teuchos::rcp(new CrsMatrixWrap(ccMat));
  Teuchos::RCP<Matrix> curlcurlOp = Teuchos::rcp_dynamic_cast<Matrix>(ccOp);

  Teuchos::RCP<CrsMatrix> d0cMat = Teuchos::rcp(new EpetraCrsMatrix(Teuchos::rcpFromRef(D0clean)));
  Teuchos::RCP<CrsMatrixWrap> d0cOp = Teuchos::rcp(new CrsMatrixWrap(d0cMat));
  Teuchos::RCP<Matrix> D0cleanOp = Teuchos::rcp_dynamic_cast<Matrix>(d0cOp);

  Teuchos::RCP<CrsMatrix> m0invMat = Teuchos::rcp(new EpetraCrsMatrix(Teuchos::rcpFromRef(M0inv)));
  Teuchos::RCP<CrsMatrixWrap> m0invOp = Teuchos::rcp(new CrsMatrixWrap(m0invMat));
  Teuchos::RCP<Matrix> M0invOp = Teuchos::rcp_dynamic_cast<Matrix>(m0invOp);

  Teuchos::RCP<CrsMatrix> m1Mat = Teuchos::rcp(new EpetraCrsMatrix(Teuchos::rcpFromRef(M1)));
  Teuchos::RCP<CrsMatrixWrap> m1Op = Teuchos::rcp(new CrsMatrixWrap(m1Mat));
  Teuchos::RCP<Matrix> M1Op = Teuchos::rcp_dynamic_cast<Matrix>(m1Op);

  Teuchos::RCP<CrsMatrix> msMat = Teuchos::rcp(new EpetraCrsMatrix(Teuchos::rcpFromRef(Ms)));
  Teuchos::RCP<CrsMatrixWrap> msOp = Teuchos::rcp(new CrsMatrixWrap(msMat));
  Teuchos::RCP<Matrix> MsOp = Teuchos::rcp_dynamic_cast<Matrix>(msOp);

  Teuchos::RCP<MultiVector> xxh = Teuchos::rcp(new EpetraMultiVector(Teuchos::rcpFromRef(xh)));
  Teuchos::RCP<MultiVector> xb  = Teuchos::rcp(new EpetraMultiVector(Teuchos::rcpFromRef(b)));

  Teuchos::RCP<MultiVector> xcoords = Teuchos::rcp(new EpetraMultiVector(Teuchos::rcpFromRef(coords)));

  MLList.set("parameterlist: syntax","muelu");

  // construct preconditioner
  Teuchos::RCP<MueLu::RefMaxwell<SC,LO,GO,NO> > preconditioner
  = Teuchos::rcp( new MueLu::RefMaxwell<SC,LO,GO,NO>(curlcurlOp,d0cOp,MsOp,M0invOp,
                                                       M1Op,Teuchos::null,xcoords,MLList) );

  MueLu::AztecEpetraOperator prec(preconditioner);

  if(CurlCurl.Comm().MyPID()==0) {std::cout << "Setup time: " << SetupTime.ElapsedTime()
                                            << " sec \n"; SetupTime.ResetStartTime();}

  Epetra_MultiVector x(xh);
  x.PutScalar(0.0);
  Epetra_LinearProblem Problem(&CurlCurl,&x,&b);
  Epetra_MultiVector* lhs = Problem.GetLHS();
  Epetra_MultiVector* rhs = Problem.GetRHS();

  Epetra_Time Time(CurlCurl.Comm());

  AztecOO solver(Problem);
  solver.SetAztecOption(AZ_solver, AZ_cg);
  solver.SetAztecOption(AZ_output, 10);
  solver.SetPrecOperator(&prec);
  solver.Iterate(500, 1e-10);

  // accuracy check
  Epetra_MultiVector xexact(xh);
  xexact.PutScalar(0.0);

  string msg = ProblemType;
  solution_test(msg,CurlCurl,*lhs,*rhs,xexact,Time,TotalErrorExactSol,TotalErrorResidual);

  xh = *lhs;
#endif
}

/*************************************************************************************/
/*************************** ML PRECONDITIONER****************************************/
/*************************************************************************************/
void TestMultiLevelPreconditioner_Maxwell(char ProblemType[],
                                          Teuchos::ParameterList   & MLList,
                                          Epetra_CrsMatrix   & CurlCurl,
                                          Epetra_CrsMatrix   & D0clean,
                                          Epetra_CrsMatrix   & M0inv,
                                          Epetra_CrsMatrix   & Ms,
                                          Epetra_CrsMatrix   & M1,
                                          Epetra_MultiVector & xh,
                                          Epetra_MultiVector & b,
                                          double & TotalErrorResidual,
                                          double & TotalErrorExactSol){
  /* Build RMP */

  Epetra_Time SetupTime(CurlCurl.Comm());

  ML_Epetra::RefMaxwellPreconditioner RMP(CurlCurl,D0clean,Ms,M0inv,M1,MLList);

  if(CurlCurl.Comm().MyPID()==0) {std::cout << "Setup time: " << SetupTime.ElapsedTime()
                                            << " sec \n"; SetupTime.ResetStartTime();}

  /* Build the AztecOO stuff */
  Epetra_MultiVector x(xh);
  x.PutScalar(0.0);

  Epetra_LinearProblem Problem(&CurlCurl,&x,&b);
  Epetra_MultiVector* lhs = Problem.GetLHS();
  Epetra_MultiVector* rhs = Problem.GetRHS();

  Epetra_Time Time(CurlCurl.Comm());

  /* Solve! */
  AztecOO solver(Problem);
  solver.SetPrecOperator(&RMP);
  solver.SetAztecOption(AZ_solver, AZ_cg);
  solver.SetAztecOption(AZ_output, 10);
  solver.Iterate(500, 1e-10);

  // accuracy check
  Epetra_MultiVector xexact(xh);
  xexact.PutScalar(0.0);

  string msg = ProblemType;
  solution_test(msg,CurlCurl,*lhs,*rhs,xexact,Time,TotalErrorExactSol,TotalErrorResidual);

  xh = *lhs;
}


/*************************************************************************************/
/*************************** ML PRECONDITIONER****************************************/
/*************************************************************************************/
#ifdef HAVE_TRILINOSCOUPLINGS_STRATIMIKOS
void TestMultiLevelPreconditioner_Stratimikos(char ProblemType[],
                                              Teuchos::ParameterList   & MLList,
                                              Epetra_CrsMatrix   & CurlCurl,
                                              Epetra_CrsMatrix   & D0clean,
                                              Epetra_CrsMatrix   & M0inv,
                                              Epetra_CrsMatrix   & Ms,
                                              Epetra_CrsMatrix   & M1,
                                              Epetra_MultiVector & xh,
                                              Epetra_MultiVector & b,
                                              double & TotalErrorResidual,
                                              double & TotalErrorExactSol){
  using Teuchos::rcp;
  using Teuchos::RCP;

  /* Add matrices to parameterlist */
  MLList.set("D0",rcp((const Epetra_CrsMatrix*) &D0clean,false));
  MLList.set("M0inv",rcp((const Epetra_CrsMatrix*) &M0inv,false));
  MLList.set("M1",rcp((const Epetra_CrsMatrix*) &M1,false));
  MLList.set("Ms",rcp((const Epetra_CrsMatrix*) &Ms,false));


  /* Build the rest of the Stratimikos list */
  Teuchos::ParameterList SList;
  SList.set("Linear Solver Type","AztecOO");
  SList.sublist("Linear Solver Types").sublist("AztecOO").sublist("Forward Solve").sublist("AztecOO Settings").set("Aztec Solver","CG");
  SList.sublist("Linear Solver Types").sublist("AztecOO").sublist("Forward Solve").sublist("AztecOO Settings").set("Output Frequency",10);
  SList.sublist("Linear Solver Types").sublist("AztecOO").sublist("Forward Solve").set("Max Iterations",500);
  SList.sublist("Linear Solver Types").sublist("AztecOO").sublist("Forward Solve").set("Tolerance",1e-10);
  SList.sublist("Linear Solver Types").sublist("AztecOO").set("Output Every RHS",true);
  SList.set("Preconditioner Type","ML");
  SList.sublist("Preconditioner Types").sublist("ML").set("Base Method Defaults","refmaxwell");
  SList.sublist("Preconditioner Types").sublist("ML").set("ML Settings",MLList);

  Epetra_Time Time(CurlCurl.Comm());

  /* Thyra Wrappers */
  Epetra_MultiVector x(xh);
  x.PutScalar(0.0);

  RCP<const Thyra::LinearOpBase<double> > At = Thyra::epetraLinearOp( rcp(&CurlCurl,false) );
  RCP<Thyra::MultiVectorBase<double> > xt         = Thyra::create_MultiVector( rcp(&x,false), At->domain() );
  RCP<const Thyra::MultiVectorBase<double> > bt   = Thyra::create_MultiVector( rcp(&b,false), At->range() );

  /* Stratimikos setup */
  Stratimikos::DefaultLinearSolverBuilder linearSolverBuilder;
  linearSolverBuilder.setParameterList(rcp(&SList,false));
  RCP<Thyra::LinearOpWithSolveFactoryBase<double> > lowsFactory = createLinearSolveStrategy(linearSolverBuilder);
  RCP<Thyra::LinearOpWithSolveBase<double> > lows = Thyra::linearOpWithSolve<double>(*lowsFactory,At);

  /* Solve */
  Thyra::SolveStatus<double> status = Thyra::solve<double>(*lows, Thyra::NOTRANS, *bt, xt.ptr());

  // accuracy check
  Epetra_MultiVector xexact(xh);
  xexact.PutScalar(0.0);
  string msg = ProblemType;
  solution_test(msg,CurlCurl,x,b,xexact,Time,TotalErrorExactSol,TotalErrorResidual);

  xh = x;

}


/*************************************************************************************/
/*************************** MueLu PRECONDITIONER ************************************/
/*************************************************************************************/
void TestPreconditioner_Stratimikos(char ProblemType[],
                                    Teuchos::ParameterList & SList,
                                    Epetra_CrsMatrix   & CurlCurl,
                                    Epetra_CrsMatrix   & D0clean,
                                    Epetra_CrsMatrix   & M0inv,
                                    Epetra_CrsMatrix   & Ms,
                                    Epetra_CrsMatrix   & M1,
                                    Epetra_MultiVector & coords,
                                    Epetra_Vector      & Nx,
                                    Epetra_Vector      & Ny,
                                    Epetra_Vector      & Nz,
                                    Epetra_MultiVector & xh,
                                    Epetra_MultiVector & b,
                                    double & TotalErrorResidual,
                                    double & TotalErrorExactSol){
  using Teuchos::rcp;
  using Teuchos::RCP;

  Epetra_Time SetupTime(CurlCurl.Comm());

  /* Build the rest of the Stratimikos list */
  SList.sublist("Preconditioner Types").sublist("ML").set("Base Method Defaults","refmaxwell");

  SList.sublist("Preconditioner Types").sublist("ML").sublist("ML Settings").set("D0","substitute const D0");
  SList.sublist("Preconditioner Types").sublist("ML").sublist("ML Settings").set("M0inv","substitute const M0inv");
  SList.sublist("Preconditioner Types").sublist("ML").sublist("ML Settings").set("M1","substitute const M1");
  SList.sublist("Preconditioner Types").sublist("ML").sublist("ML Settings").set("Ms","substitute const Ms");
  SList.sublist("Preconditioner Types").sublist("ML").sublist("ML Settings").sublist("refmaxwell: 11list").set("x-coordinates","substitute x-coordinates");
  SList.sublist("Preconditioner Types").sublist("ML").sublist("ML Settings").sublist("refmaxwell: 11list").set("y-coordinates","substitute y-coordinates");
  SList.sublist("Preconditioner Types").sublist("ML").sublist("ML Settings").sublist("refmaxwell: 11list").set("z-coordinates","substitute z-coordinates");
  SList.sublist("Preconditioner Types").sublist("ML").sublist("ML Settings").sublist("refmaxwell: 22list").set("x-coordinates","substitute x-coordinates");
  SList.sublist("Preconditioner Types").sublist("ML").sublist("ML Settings").sublist("refmaxwell: 22list").set("y-coordinates","substitute y-coordinates");
  SList.sublist("Preconditioner Types").sublist("ML").sublist("ML Settings").sublist("refmaxwell: 22list").set("z-coordinates","substitute z-coordinates");

  SList.sublist("Preconditioner Types").sublist("MueLuRefMaxwell").set("D0","substitute D0");
  SList.sublist("Preconditioner Types").sublist("MueLuRefMaxwell").set("M0inv","substitute M0inv");
  SList.sublist("Preconditioner Types").sublist("MueLuRefMaxwell").set("M1","substitute M1");
  SList.sublist("Preconditioner Types").sublist("MueLuRefMaxwell").set("Ms","substitute Ms");
  SList.sublist("Preconditioner Types").sublist("MueLuRefMaxwell").set("Coordinates","substitute Coordinates");

  /* Add matrices to parameterlist */
  Teuchos::ParameterList& precTypesPL = SList.sublist("Preconditioner Types");
  std::list<Teuchos::ParameterList*> sublists;
  sublists.push_back(&precTypesPL);
  while (sublists.size() > 0) {
    Teuchos::ParameterList* sublist = sublists.front();
    sublists.pop_front();
    std::list<std::string> keys;
    for (Teuchos::ParameterList::ConstIterator it = sublist->begin(); it != sublist->end(); ++it) {
      const std::string &entryName = sublist->name(it);
      const Teuchos::ParameterEntry &theEntry = sublist->entry(it);
      if (theEntry.isList()) {
        Teuchos::ParameterList& sl = sublist->sublist(entryName);
        sublists.push_back(&sl);
      } else if (theEntry.isType<std::string>() && Teuchos::getValue<std::string>(theEntry).find("substitute ") != std::string::npos) {
        keys.push_back(entryName);
      }
    }
    for (auto key_it = keys.begin(); key_it != keys.end(); ++key_it) {
      std::string value = sublist->get<std::string>(*key_it).substr(11, std::string::npos);
      if (value == "D0")
        sublist->set(*key_it, rcp((Epetra_CrsMatrix*) &D0clean,false));
      else if (value == "M0inv")
        sublist->set(*key_it, rcp((Epetra_CrsMatrix*) &M0inv,false));
      else if (value == "M1")
        sublist->set(*key_it, rcp((Epetra_CrsMatrix*) &M1,false));
      else if (value == "Ms")
        sublist->set(*key_it, rcp((Epetra_CrsMatrix*) &Ms,false));
      else if (value == "Coordinates")
        sublist->set(*key_it, rcp((Epetra_MultiVector*) &coords,false));
      else if (value == "const D0")
        sublist->set(*key_it, rcp((const Epetra_CrsMatrix*) &D0clean,false));
      else if (value == "const M0inv")
        sublist->set(*key_it, rcp((const Epetra_CrsMatrix*) &M0inv,false));
      else if (value == "const M1")
        sublist->set(*key_it, rcp((const Epetra_CrsMatrix*) &M1,false));
      else if (value == "const Ms")
        sublist->set(*key_it, rcp((const Epetra_CrsMatrix*) &Ms,false));
      else if (value == "const Coordinates")
        sublist->set(*key_it, rcp((const Epetra_MultiVector*) &coords,false));
      else if (value == "x-coordinates")
        sublist->set(*key_it, &Nx[0]);
      else if (value == "y-coordinates")
        sublist->set(*key_it, &Ny[0]);
      else if (value == "z-coordinates")
        sublist->set(*key_it, &Nz[0]);
    }
  }

  Epetra_Time Time(CurlCurl.Comm());

  /* Thyra Wrappers */
  Epetra_MultiVector x(xh);
  x.PutScalar(0.0);

  RCP<const Thyra::LinearOpBase<double> > At = Thyra::epetraLinearOp( rcp(&CurlCurl,false) );
  RCP<Thyra::MultiVectorBase<double> > xt         = Thyra::create_MultiVector( rcp(&x,false), At->domain() );
  RCP<const Thyra::MultiVectorBase<double> > bt   = Thyra::create_MultiVector( rcp(&b,false), At->range() );

  /* Stratimikos setup */
  Stratimikos::DefaultLinearSolverBuilder linearSolverBuilder;
#if defined(HAVE_MUELU_EPETRA) and defined(HAVE_TRILINOSCOUPLINGS_MUELU)
  // typedef double Scalar;
  typedef int LocalOrdinal;
  typedef int GlobalOrdinal;
  typedef LocalOrdinal LO;
  typedef GlobalOrdinal GO;
  typedef Xpetra::EpetraNode Node;
  Stratimikos::enableMueLuRefMaxwell<double,LO,GO,Node>(linearSolverBuilder);                // Register MueLu as a Stratimikos preconditioner strategy.
#endif
  linearSolverBuilder.setParameterList(rcp(&SList,false));
  RCP<Thyra::LinearOpWithSolveFactoryBase<double> > lowsFactory = createLinearSolveStrategy(linearSolverBuilder);
  RCP<Thyra::LinearOpWithSolveBase<double> > lows = Thyra::linearOpWithSolve<double>(*lowsFactory,At);

  /* Solve */
  Thyra::SolveStatus<double> status = Thyra::solve<double>(*lows, Thyra::NOTRANS, *bt, xt.ptr());

  // accuracy check
  Epetra_MultiVector xexact(xh);
  xexact.PutScalar(0.0);
  string msg = ProblemType;
  solution_test(msg,CurlCurl,x,b,xexact,Time,TotalErrorExactSol,TotalErrorResidual);

  xh = x;
}

#endif  // HAVE_TRILINOSCOUPLINGS_STRATIMIKOS

/**********************************************************************************/
/************ USER DEFINED FUNCTIONS FOR EXACT SOLUTION ***************************/
/**********************************************************************************/

// Calculates value of exact solution u
int evalu(double & uExact0, double & uExact1, double & uExact2, double & x, double & y, double & z)
{

  // Exact solution 1 - homogeneous boundary conditions
  uExact0 = sin(M_PI*x)*sin(M_PI*y);
  uExact1 = y;
  uExact2 = z;

  return 0;
}

// Calculates curl of exact solution u
int evalCurlu(double & curlu0, double & curlu1, double & curlu2, double & x, double & y, double & z, double &mu)
{
  // Exact solution 1 - homogeneous boundary conditions
  curlu0 = 0;
  curlu1 = 0;
  curlu2 = -mu*M_PI*(sin(M_PI*x)*cos(M_PI*y));

  return 0;
}



// Calculates curl of exact solution u
int evalCurlCurlu(double & curlcurlu0, double & curlcurlu1, double & curlcurlu2, double & x, double & y, double & z, double &mu)
{
  // Exact solution 1 - homogeneous boundary conditions
  curlcurlu0 = mu*M_PI*M_PI*(sin(M_PI*x)*sin(M_PI*y));
  curlcurlu1 = mu*M_PI*M_PI*(cos(M_PI*x)*cos(M_PI*y));
  curlcurlu2 = 0;

  return 0;
}
