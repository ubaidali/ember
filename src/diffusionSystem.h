#pragma once

#include "mathUtils.h"
#include "sundialsUtils.h"
#include "chemistry0d.h"
#include "grid.h"
#include "integrator.h"

class DiffusionSystem : public LinearODE, public GridBased
{
    // This is a system representing diffusion of a single solution component,
    // represented by an ODE in one of the following forms:
    //     ydot = B * d/dx(D * dy/dx) + C
    //     ydot = B/r * d/dr(r * D * dy/dr) + C
    // The ODE in this form may be written as a linear system:
    //     ydot = Ay + C
    // where the entries of the matrix A are determined by the prefactor B,
    // the diffusion coefficients D, and the finite difference formula used.

public:

    // Provide the matrix associated with the ODE to the integrator
    void get_A(sdBandMatrix& J);

    // Provides the constant term to the integrator
    void get_C(dvector& y);

    // the coefficients of the ODE
    dvector B; // pre-factor
    dvector D; // "diffusion" coefficient

    // Diagonalized, linear approximations for terms neglected by splitting
    dvector splitConst; // constant terms
    dvector splitLinear; // diagonal jacobian components
};