// Copyright 2002 - 2008, 2010, 2011 National Technology Engineering
// Solutions of Sandia, LLC (NTESS). Under the terms of Contract
// DE-NA0003525 with NTESS, the U.S. Government retains certain rights
// in this software.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef KRINO_UNIT_TESTS_INCLUDE_AKRI_UNITTESTUTILS_H_
#define KRINO_UNIT_TESTS_INCLUDE_AKRI_UNITTESTUTILS_H_
#include <Akri_Vec.hpp>

namespace krino {

void expect_eq(const Vector3d & gold, const Vector3d & result, const double relativeTol=1.e-6);
void expect_eq_absolute(const Vector3d & gold, const Vector3d & result, const double absoluteTol=1.e-6);

}

#endif /* KRINO_UNIT_TESTS_INCLUDE_AKRI_UNITTESTUTILS_H_ */
