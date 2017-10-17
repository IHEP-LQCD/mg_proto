/*
 * qphix_testutils.h
 *
 *  Created on: Oct 17, 2017
 *      Author: bjoo
 */
#ifndef QPHIX_TESTUTILS_H
#define QPHIX_TESTUTILS_H

#include "qdp.h"
#include "lattice/qphix/qphix_types.h"


namespace MGTesting
{
void DiffCBSpinor(const QDP::LatticeFermion& s1, const MG::QPhiXSpinor& qphix_spinor, int cb, double tol);
void DiffSpinor(const QDP::LatticeFermion& s1, const MG::QPhiXSpinor& qphix_spinor, double tol);
void DiffSpinor(const QDP::LatticeFermion& s1, const QDP::LatticeFermion& s2, double tol);
void DiffSpinorRelative(const QDP::LatticeFermion& b, const QDP::LatticeFermion& Ax, double tol);
}
#endif


