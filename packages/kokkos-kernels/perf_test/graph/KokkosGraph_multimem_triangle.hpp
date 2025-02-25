//@HEADER
// ************************************************************************
//
//                        Kokkos v. 4.0
//       Copyright (2022) National Technology & Engineering
//               Solutions of Sandia, LLC (NTESS).
//
// Under the terms of Contract DE-NA0003525 with NTESS,
// the U.S. Government retains certain rights in this software.
//
// Part of Kokkos, under the Apache License v2.0 with LLVM Exceptions.
// See https://kokkos.org/LICENSE for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//@HEADER

#include "KokkosGraph_run_triangle.hpp"
#include "KokkosSparse_CrsMatrix.hpp"

namespace KokkosKernels {

namespace Experiment {

template <typename size_type, typename lno_t, typename exec_space,
          typename hbm_mem_space, typename sbm_mem_space>
void run_multi_mem_triangle(Parameters params) {
  typedef exec_space myExecSpace;
  typedef Kokkos::Device<exec_space, hbm_mem_space> myFastDevice;
  typedef Kokkos::Device<exec_space, sbm_mem_space> mySlowExecSpace;

  typedef typename KokkosSparse::CrsMatrix<double, lno_t, myFastDevice, void,
                                           size_type>
      fast_crstmat_t;
  typedef typename fast_crstmat_t::StaticCrsGraphType fast_graph_t;

  typedef typename KokkosSparse::CrsMatrix<double, lno_t, mySlowExecSpace, void,
                                           size_type>
      slow_crstmat_t;
  typedef typename slow_crstmat_t::StaticCrsGraphType slow_graph_t;

  char *a_mat_file = params.a_mtx_bin_file;
  // char *b_mat_file = params.b_mtx_bin_file;
  // char *c_mat_file = params.c_mtx_bin_file;

  slow_graph_t a_slow_crsgraph, /*b_slow_crsgraph,*/ c_slow_crsgraph;
  fast_graph_t a_fast_crsgraph, /*b_fast_crsgraph,*/ c_fast_crsgraph;

  // read a and b matrices and store them on slow or fast memory.
  if (params.a_mem_space == 1) {
    fast_crstmat_t a_fast_crsmat;
    a_fast_crsmat =
        KokkosKernels::Impl::read_kokkos_crst_matrix<fast_crstmat_t>(
            a_mat_file);
    a_fast_crsgraph          = a_fast_crsmat.graph;
    a_fast_crsgraph.num_cols = a_fast_crsmat.numCols();

  } else {
    slow_crstmat_t a_slow_crsmat;
    a_slow_crsmat =
        KokkosKernels::Impl::read_kokkos_crst_matrix<slow_crstmat_t>(
            a_mat_file);
    a_slow_crsgraph          = a_slow_crsmat.graph;
    a_slow_crsgraph.num_cols = a_slow_crsmat.numCols();
  }

  if (params.a_mem_space == 1) {
    if (params.b_mem_space == 1) {
      if (params.c_mem_space == 1) {
        if (params.work_mem_space == 1) {
          /* c_fast_crsgraph = */
          KokkosKernels::Experiment::run_experiment<
              myExecSpace, fast_graph_t, fast_graph_t, fast_graph_t,
              hbm_mem_space, hbm_mem_space>(a_fast_crsgraph,
                                            /*b_fast_crsgraph,*/ params);
        } else {
          /* c_fast_crsgraph = */
          KokkosKernels::Experiment::run_experiment<
              myExecSpace, fast_graph_t, fast_graph_t, fast_graph_t,
              sbm_mem_space, sbm_mem_space>(a_fast_crsgraph,
                                            /*b_fast_crsgraph,*/ params);
        }

      } else {
        // C is in slow memory.
        if (params.work_mem_space == 1) {
          /*c_slow_crsgraph =*/
          KokkosKernels::Experiment::run_experiment<
              myExecSpace, fast_graph_t, fast_graph_t, slow_graph_t,
              hbm_mem_space, hbm_mem_space>(a_fast_crsgraph,
                                            /*b_fast_crsgraph,*/ params);
        } else {
          /*c_slow_crsgraph =*/
          KokkosKernels::Experiment::run_experiment<
              myExecSpace, fast_graph_t, fast_graph_t, slow_graph_t,
              sbm_mem_space, sbm_mem_space>(a_fast_crsgraph,
                                            /*b_fast_crsgraph,*/ params);
        }
      }
    } else {
      // B is in slow memory
      if (params.c_mem_space == 1) {
        if (params.work_mem_space == 1) {
          /* c_fast_crsgraph = */
          KokkosKernels::Experiment::run_experiment<
              myExecSpace, fast_graph_t, slow_graph_t, fast_graph_t,
              hbm_mem_space, hbm_mem_space>(a_fast_crsgraph,
                                            /*b_slow_crsgraph,*/ params);
        } else {
          /* c_fast_crsgraph = */
          KokkosKernels::Experiment::run_experiment<
              myExecSpace, fast_graph_t, slow_graph_t, fast_graph_t,
              sbm_mem_space, sbm_mem_space>(a_fast_crsgraph,
                                            /*b_slow_crsgraph,*/ params);
        }

      } else {
        // C is in slow memory.
        if (params.work_mem_space == 1) {
          /*c_slow_crsgraph =*/
          KokkosKernels::Experiment::run_experiment<
              myExecSpace, fast_graph_t, slow_graph_t, slow_graph_t,
              hbm_mem_space, hbm_mem_space>(a_fast_crsgraph,
                                            /*b_slow_crsgraph,*/ params);
        } else {
          /*c_slow_crsgraph =*/
          KokkosKernels::Experiment::run_experiment<
              myExecSpace, fast_graph_t, slow_graph_t, slow_graph_t,
              sbm_mem_space, sbm_mem_space>(a_fast_crsgraph,
                                            /*b_slow_crsgraph,*/ params);
        }
      }
    }
  } else {
    // A is in slow memory
    if (params.b_mem_space == 1) {
      if (params.c_mem_space == 1) {
        if (params.work_mem_space == 1) {
          /* c_fast_crsgraph = */
          KokkosKernels::Experiment::run_experiment<
              myExecSpace, slow_graph_t, fast_graph_t, fast_graph_t,
              hbm_mem_space, hbm_mem_space>(a_slow_crsgraph,
                                            /*b_fast_crsgraph,*/ params);
        } else {
          /* c_fast_crsgraph = */
          KokkosKernels::Experiment::run_experiment<
              myExecSpace, slow_graph_t, fast_graph_t, fast_graph_t,
              sbm_mem_space, sbm_mem_space>(a_slow_crsgraph,
                                            /*b_fast_crsgraph,*/ params);
        }

      } else {
        // C is in slow memory.
        if (params.work_mem_space == 1) {
          /*c_slow_crsgraph =*/
          KokkosKernels::Experiment::run_experiment<
              myExecSpace, slow_graph_t, fast_graph_t, slow_graph_t,
              hbm_mem_space, hbm_mem_space>(a_slow_crsgraph,
                                            /*b_fast_crsgraph,*/ params);
        } else {
          /*c_slow_crsgraph =*/
          KokkosKernels::Experiment::run_experiment<
              myExecSpace, slow_graph_t, fast_graph_t, slow_graph_t,
              sbm_mem_space, sbm_mem_space>(a_slow_crsgraph,
                                            /*b_fast_crsgraph,*/ params);
        }
      }
    } else {
      // B is in slow memory
      if (params.c_mem_space == 1) {
        if (params.work_mem_space == 1) {
          /* c_fast_crsgraph = */
          KokkosKernels::Experiment::run_experiment<
              myExecSpace, slow_graph_t, slow_graph_t, fast_graph_t,
              hbm_mem_space, hbm_mem_space>(a_slow_crsgraph,
                                            /*b_slow_crsgraph,*/ params);
        } else {
          /* c_fast_crsgraph = */
          KokkosKernels::Experiment::run_experiment<
              myExecSpace, slow_graph_t, slow_graph_t, fast_graph_t,
              sbm_mem_space, sbm_mem_space>(a_slow_crsgraph,
                                            /*b_slow_crsgraph,*/ params);
        }

      } else {
        // C is in slow memory.
        if (params.work_mem_space == 1) {
          /*c_slow_crsgraph =*/
          KokkosKernels::Experiment::run_experiment<
              myExecSpace, slow_graph_t, slow_graph_t, slow_graph_t,
              hbm_mem_space, hbm_mem_space>(a_slow_crsgraph,
                                            /*b_slow_crsgraph,*/ params);
        } else {
          /*c_slow_crsgraph =*/
          KokkosKernels::Experiment::run_experiment<
              myExecSpace, slow_graph_t, slow_graph_t, slow_graph_t,
              sbm_mem_space, sbm_mem_space>(a_slow_crsgraph,
                                            /*b_slow_crsgraph,*/ params);
        }
      }
    }
  }
}

}  // namespace Experiment
}  // namespace KokkosKernels
