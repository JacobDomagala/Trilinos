// @HEADER
//
// ***********************************************************************
//
//   Zoltan2: A package of combinatorial algorithms for scientific computing
//                  Copyright 2012 Sandia Corporation
//
// Under the terms of Contract DE-AC04-94AL85000 with Sandia Corporation,
// the U.S. Government retains certain rights in this software.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// 1. Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// 3. Neither the name of the Corporation nor the names of the
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY SANDIA CORPORATION "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL SANDIA CORPORATION OR THE
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Questions? Contact Karen Devine      (kddevin@sandia.gov)
//                    Erik Boman        (egboman@sandia.gov)
//                    Siva Rajamanickam (srajama@sandia.gov)
//
// ***********************************************************************
//
// @HEADER
//
// Basic testing of Zoltan2::TpetraRowMatrixAdapter

/*! \file TpetraRowMatrixInput.cpp
 *  \brief Test of Zoltan2::TpetraRowMatrixAdapter class.
 *  \todo test with geometric row coordinates.
 */

#include <Zoltan2_InputTraits.hpp>
#include <Zoltan2_TestHelpers.hpp>
#include <Zoltan2_TpetraRowMatrixAdapter.hpp>
#include <Zoltan2_TpetraCrsMatrixAdapter.hpp>

#include <Teuchos_Comm.hpp>
#include <Teuchos_CommHelpers.hpp>
#include <Teuchos_DefaultComm.hpp>
#include <Teuchos_RCP.hpp>
#include <cstdlib>
#include <stdexcept>

using Teuchos::Comm;
using Teuchos::Comm;
using Teuchos::RCP;
using Teuchos::rcp;
using Teuchos::rcp_const_cast;
using Teuchos::rcp_dynamic_cast;

using ztcrsmatrix_t = Tpetra::CrsMatrix<zscalar_t, zlno_t, zgno_t, znode_t>;
using ztrowmatrix_t = Tpetra::RowMatrix<zscalar_t, zlno_t, zgno_t, znode_t>;
using node_t = typename Zoltan2::InputTraits<ztrowmatrix_t>::node_t;
using device_t = typename node_t::device_type;
using rowAdapter_t = Zoltan2::TpetraRowMatrixAdapter<ztrowmatrix_t>;
using crsAdapter_t = Zoltan2::TpetraCrsMatrixAdapter<ztcrsmatrix_t>;
using execspace_t =
    typename rowAdapter_t::ConstWeightsHostView1D::execution_space;

//////////////////////////////////////////////////////////////////////////

template<typename offset_t>
void printMatrix(RCP<const Comm<int> > &comm, zlno_t nrows,
    const zgno_t *rowIds, const offset_t *offsets, const zgno_t *colIds) {
  int rank = comm->getRank();
  int nprocs = comm->getSize();
  comm->barrier();
  for (int p=0; p < nprocs; p++){
    if (p == rank){
      std::cout << rank << ":" << std::endl;
      for (zlno_t i=0; i < nrows; i++){
        std::cout << " row " << rowIds[i] << ": ";
        for (offset_t j=offsets[i]; j < offsets[i+1]; j++){
          std::cout << colIds[j] << " ";
        }
        std::cout << std::endl;
      }
      std::cout.flush();
    }
    comm->barrier();
  }
  comm->barrier();
}

//////////////////////////////////////////////////////////////////////////

template <typename adapter_t, typename matrix_t>
void TestMatrixIds(adapter_t &ia, matrix_t &matrix) {

  using idsHost_t = typename adapter_t::ConstIdsHostView;
  using offsetsHost_t = typename adapter_t::ConstOffsetsHostView;
  using localInds_t =
      typename adapter_t::user_t::nonconst_local_inds_host_view_type;
  using localVals_t =
      typename adapter_t::user_t::nonconst_values_host_view_type;


  const auto nrows = matrix.getLocalNumRows();
  const auto ncols = matrix.getLocalNumEntries();
  const auto maxNumEntries = matrix.getLocalMaxNumRowEntries();

  typename adapter_t::Base::ConstIdsHostView colIdsHost_("colIdsHost_", ncols);
  typename adapter_t::Base::ConstOffsetsHostView offsHost_("offsHost_",
                                                           nrows + 1);

  localInds_t localColInds("localColInds", maxNumEntries);
  localVals_t localVals("localVals", maxNumEntries);

  for (size_t r = 0; r < nrows; r++) {
    size_t numEntries = 0;
    matrix.getLocalRowCopy(r, localColInds, localVals, numEntries);;

    offsHost_(r + 1) = offsHost_(r) + numEntries;
    for (size_t e = offsHost_(r), i = 0; e < offsHost_(r + 1); e++) {
      colIdsHost_(e) = matrix.getColMap()->getGlobalElement(localColInds(i++));
    }
  }

  idsHost_t rowIdsHost;
  ia.getRowIDsHostView(rowIdsHost);

  const auto matrixIDS = matrix.getRowMap()->getLocalElementList();

  Z2_TEST_COMPARE_ARRAYS(matrixIDS, rowIdsHost);

  idsHost_t colIdsHost;
  offsetsHost_t offsetsHost;
  ia.getCRSHostView(offsetsHost, colIdsHost);

  Z2_TEST_COMPARE_ARRAYS(colIdsHost_, colIdsHost);
  Z2_TEST_COMPARE_ARRAYS(offsHost_, offsetsHost);
}

template <typename adapter_t, typename matrix_t>
void verifyInputAdapter(adapter_t &ia, matrix_t &matrix) {
  using idsDevice_t = typename adapter_t::ConstIdsDeviceView;
  using idsHost_t = typename adapter_t::ConstIdsHostView;
  using offsetsDevice_t = typename adapter_t::ConstOffsetsDeviceView;
  using offsetsHost_t = typename adapter_t::ConstOffsetsHostView;
  using weightsDevice_t = typename adapter_t::WeightsDeviceView1D;
  using weightsHost_t = typename adapter_t::WeightsHostView1D;
  using constWeightsDevice_t = typename adapter_t::ConstWeightsDeviceView1D;
  using constWeightsHost_t = typename adapter_t::ConstWeightsHostView1D;

  const auto nrows = ia.getLocalNumIDs();

  Z2_TEST_EQUALITY(ia.getLocalNumRows(), matrix.getLocalNumRows());
  Z2_TEST_EQUALITY(ia.getLocalNumColumns(), matrix.getLocalNumCols());
  Z2_TEST_EQUALITY(ia.getLocalNumEntries(), matrix.getLocalNumEntries());

  /////////////////////////////////
  //// getRowIdsView
  /////////////////////////////////

  idsDevice_t rowIdsDevice;
  ia.getRowIDsDeviceView(rowIdsDevice);
  idsHost_t rowIdsHost;
  ia.getRowIDsHostView(rowIdsHost);

  TestDeviceHostView(rowIdsDevice, rowIdsHost);

  /////////////////////////////////
  //// setRowWeightsDevice
  /////////////////////////////////
  Z2_TEST_THROW(ia.setRowWeightsDevice(
                    typename adapter_t::WeightsDeviceView1D{}, 50),
                std::runtime_error);

  weightsDevice_t wgts0("wgts0", nrows);
  Kokkos::parallel_for(
      nrows, KOKKOS_LAMBDA(const int idx) { wgts0(idx) = idx * 2; });

  Z2_TEST_NOTHROW(ia.setRowWeightsDevice(wgts0, 0));

  // Don't reuse the same View, since we don't copy the values,
  // we just assign the View (increase use count)
  weightsDevice_t wgts1("wgts1", nrows);
  Kokkos::parallel_for(
      nrows, KOKKOS_LAMBDA(const int idx) { wgts1(idx) = idx * 3; });

  Z2_TEST_NOTHROW(ia.setRowWeightsDevice(wgts1, 1));

  /////////////////////////////////
  //// getRowWeightsDevice
  /////////////////////////////////
  {
    weightsDevice_t weightsDevice;
    Z2_TEST_NOTHROW(ia.getRowWeightsDeviceView(weightsDevice, 0));

    weightsHost_t weightsHost;
    Z2_TEST_NOTHROW(ia.getRowWeightsHostView(weightsHost, 0));

    TestDeviceHostView(weightsDevice, weightsHost);

    TestDeviceHostView(wgts0, weightsHost);
  }
  {
    weightsDevice_t weightsDevice;
    Z2_TEST_NOTHROW(ia.getRowWeightsDeviceView(weightsDevice, 1));

    weightsHost_t weightsHost;
    Z2_TEST_NOTHROW(ia.getRowWeightsHostView(weightsHost, 1));

    TestDeviceHostView(weightsDevice, weightsHost);

    TestDeviceHostView(wgts1, weightsHost);
  }
  {
    weightsDevice_t wgtsDevice;
    Z2_TEST_THROW(ia.getRowWeightsDeviceView(wgtsDevice, 2),
                  std::runtime_error);

    weightsHost_t wgtsHost;
    Z2_TEST_THROW(ia.getRowWeightsHostView(wgtsHost, 2), std::runtime_error);
  }

  TestMatrixIds(ia, matrix);
}

//////////////////////////////////////////////////////////////////////////

int main(int narg, char *arg[]) {
  using rowSoln_t = Zoltan2::PartitioningSolution<rowAdapter_t>;
  using rowPart_t = rowAdapter_t::part_t;

  using crsSoln_t = Zoltan2::PartitioningSolution<crsAdapter_t>;
  using crsPart_t = crsAdapter_t::part_t;

  Tpetra::ScopeGuard tscope(&narg, &arg);
  const auto comm = Tpetra::getDefaultComm();

  auto rank = comm->getRank();

  try {
    Teuchos::ParameterList params;
    params.set("input file", "simple");
    params.set("file type", "Chaco");

    auto uinput = rcp(new UserInputForTests(params, comm));

    // Input crs matrix and row matrix cast from it.
    const auto crsMatrix = uinput->getUITpetraCrsMatrix();
    const auto rowMatrix = rcp_dynamic_cast<ztrowmatrix_t>(crsMatrix);

    const auto nrows = rowMatrix->getLocalNumRows();

    // To test migration in the input adapter we need a Solution object.
    const auto env = rcp(new Zoltan2::Environment(comm));

    const int nWeights = 2;

    /////////////////////////////////////////////////////////////
    // User object is Tpetra::RowMatrix
    /////////////////////////////////////////////////////////////

    PrintFromRoot("Input adapter for Tpetra::RowMatrix");

    // Graph Adapters use crsGraph, original TpetraInput uses trM (=rowMatrix)
    auto tpetraRowMatrixInput = rcp(new rowAdapter_t(rowMatrix, nWeights));

    verifyInputAdapter(*tpetraRowMatrixInput, *rowMatrix);

    rowPart_t *p = new rowPart_t[nrows];
    memset(p, 0, sizeof(rowPart_t) * nrows);
    ArrayRCP<rowPart_t> solnParts(p, 0, nrows, true);

    rowSoln_t solution(env, comm, nWeights);
    solution.setParts(solnParts);

    ztrowmatrix_t *mMigrate = NULL;
    tpetraRowMatrixInput->applyPartitioningSolution(*rowMatrix, mMigrate,
                                                    solution);
    const auto newM = rcp(mMigrate);
    auto cnewM = rcp_const_cast<const ztrowmatrix_t>(newM);
    auto newInput = rcp(new rowAdapter_t(cnewM, nWeights));

    PrintFromRoot("Input adapter for Tpetra::RowMatrix migrated to proc 0");

    verifyInputAdapter(*newInput, *newM);

  } catch (std::exception &e) {
    std::cout << e.what() << std::endl;
    return EXIT_FAILURE;
  }

  PrintFromRoot("PASS");

}
