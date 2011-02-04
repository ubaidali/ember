#include "convectionSystem.h"

#include <boost/foreach.hpp>
#define foreach BOOST_FOREACH

#ifdef NDEBUG
    static const bool VERY_VERBOSE = false;
#else
    static const bool VERY_VERBOSE = true;
#endif

ConvectionSystemUTW::ConvectionSystemUTW()
    : gas(NULL)
    , nVars(3)
{
}

int ConvectionSystemUTW::f(const realtype t, const sdVector& y, sdVector& ydot)
{
    unroll_y(y);
    // *** Update auxiliary data ***
    for (size_t j=0; j<nPoints; j++) {
        rho[j] = gas->pressure*Wmx[j]/(Cantera::GasConstant*T[j]);
    }

    // *** Calculate V ***
    rV[0] = rVzero;
    for (size_t j=0; j<nPoints-1; j++) {
        // Compute the upwinded convective derivative
        if (rV[j] < 0 || j == 0) {
            dTdx[j] = (T[j+1] - T[j]) / hh[j];
            dUdx[j] = (U[j+1] - U[j]) / hh[j];
            dWdx[j] = (Wmx[j+1] - Wmx[j]) / hh[j];
        } else {
            dTdx[j] = (T[j] - T[j-1]) / hh[j-1];
            dUdx[j] = (U[j] - U[j-1]) / hh[j-1];
            dWdx[j] = (Wmx[j] - Wmx[j-1]) / hh[j-1];
        }

        rV[j+1] = rV[j] - hh[j] * (rV[j] * dTdx[j] - rphalf[j] * rho[j] * dTdtSplit[j]) / T[j];
        rV[j+1] += hh[j] * (rV[j] * dWdx[j] - rphalf[j] * rho[j] * dWdtSplit[j]) / Wmx[j];
        rV[j+1] -= hh[j] * rho[j] * U[j] * rphalf[j];
    }

    rV2V();

    // *** Calculate dW/dt, dU/dt, dT/dt

    // Left boundary conditions.
    // Convection term only contributes in the ControlVolume case
    dUdt[0] = Uconst[0]; // zero-gradient condition for U no matter what

    if (grid.leftBC == BoundaryCondition::ControlVolume) {
        double centerVol = pow(x[1],alpha+1) / (alpha+1);
        double rVzero_mod = std::max(rV[0], 0.0);

        dTdt[0] = -rVzero_mod * (T[0] - Tleft) / (rho[0] * centerVol);
        dWdt[0] = -rVzero_mod * (Wmx[0] - Wleft) / (rho[0] * centerVol);

    } else { // FixedValue or ZeroGradient
        dTdt[0] = 0;
        dWdt[0] = 0;
    }

    // Intermediate points
    for (size_t j=1; j<jj; j++) {
        dUdt[j] = -V[j] * dUdx[j] / rho[j];
        dTdt[j] = -V[j] * dTdx[j] / rho[j];
        dWdt[j] = -V[j] * dWdx[j] / rho[j];
    }

    // Right boundary values
    // Convection term has nothing to contribute in any case,
    // So only the value from the other terms remains
    dUdt[jj] = 0;
    dTdt[jj] = 0;
    dWdt[jj] = 0;

    roll_ydot(ydot);
    return 0;
}

void ConvectionSystemUTW::get_diagonal
(const realtype t, dvector& linearU, dvector& linearT)
{
    // Assume that f has just been called and that all auxiliary
    // arrays are in a consistent state.

    // Left boundary conditions.
    // Convection term only contributes in the ControlVolume case

    // dUdot/dU
    linearU[0] = 0;

    if (grid.leftBC == BoundaryCondition::ControlVolume) {
        double centerVol = pow(x[1],alpha+1) / (alpha+1);
        double rVzero_mod = std::max(rV[0], 0.0);

        // dTdot/dT
        linearT[0] = -rVzero_mod / (rho[0] * centerVol);

    } else { // FixedValue or ZeroGradient
        linearT[0] = 0;
    }

    // Intermediate points
    for (size_t j=1; j<jj; j++) {
        // depends on upwinding to calculated dT/dx etc.
        double value = (rV[j] < 0 || j == 0) ? V[j] / (rho[j] * hh[j])
                                             : -V[j] / (rho[j] * hh[j-1]);
        linearU[j] = value;
        linearT[j] = value;
    }
}

int ConvectionSystemUTW::bandedJacobian(const realtype t, const sdVector& y,
                                     const sdVector& ydot, sdBandMatrix& J)
{
    // Assume that f has just been called and that all auxiliary
    // arrays are in a consistent state.

    // Left boundary conditions.
    // Convection term only contributes in the ControlVolume case

    // dUdot/dU
    J(0*nVars+kMomentum, 0*nVars+kMomentum) = 0;

    if (grid.leftBC == BoundaryCondition::ControlVolume) {
        double centerVol = pow(x[1],alpha+1) / (alpha+1);
        double rVzero_mod = std::max(rV[0], 0.0);

        // dTdot/dT
        J(0*nVars+kEnergy, 0*nVars+kEnergy) =
                -rVzero_mod / (rho[0] * centerVol);

        // dWdot/dW
        J(0*nVars+kWmx, 0*nVars+kWmx) = -rVzero_mod / (rho[0] * centerVol);

    } else { // FixedValue or ZeroGradient
        J(0*nVars+kEnergy, 0*nVars+kEnergy) = 0;
        // J(0*nVars+kWmx, 0*nVars+kWmx) = 0;
    }

    // Intermediate points
    for (size_t j=1; j<jj; j++) {
        // depends on upwinding to calculated dT/dx etc.
        double value = (rV[j] < 0 || j == 0) ? V[j] / (rho[j] * hh[j])
                                             : -V[j] / (rho[j] * hh[j-1]);
        J(j*nVars+kMomentum, j*nVars+kMomentum) = value;
        J(j*nVars+kEnergy, j*nVars+kEnergy) = value;
        J(j*nVars+kWmx, j*nVars+kWmx) = value;
    }

    // Right boundary conditions
    J(jj*nVars+kMomentum, jj*nVars+kMomentum) = 0;
    J(jj*nVars+kEnergy, jj*nVars+kEnergy) = 0;
    // J(jj*nVars+kWmx, jj*nVars+kWmx) = 0;

    return 0;
}

void ConvectionSystemUTW::unroll_y(const sdVector& y)
{
    for (size_t j=0; j<nPoints; j++) {
        T[j] = y[j*nVars+kEnergy];
        U[j] = y[j*nVars+kMomentum];
        Wmx[j] = y[j*nVars+kWmx];
    }
}

void ConvectionSystemUTW::roll_y(sdVector& y) const
{
    for (size_t j=0; j<nPoints; j++) {
        y[j*nVars+kEnergy] = T[j];
        y[j*nVars+kMomentum] = U[j];
        y[j*nVars+kWmx] = Wmx[j];
    }
}

void ConvectionSystemUTW::roll_ydot(sdVector& ydot) const
{
    for (size_t j=0; j<nPoints; j++) {
        ydot[j*nVars+kEnergy] = dTdt[j];
        ydot[j*nVars+kMomentum] = dUdt[j];
        ydot[j*nVars+kWmx] = dWdt[j];
    }
}

void ConvectionSystemUTW::resize(const size_t new_nPoints)
{
    grid.setSize(new_nPoints);
    rho.resize(nPoints);
    rV.resize(nPoints);
    V.resize(nPoints);

    U.resize(nPoints);
    dUdt.resize(nPoints);
    dUdx.resize(nPoints);
    Uconst.resize(nPoints);

    T.resize(nPoints);
    dTdt.resize(nPoints);
    dTdx.resize(nPoints);
    Tconst.resize(nPoints);
    dTdtSplit.resize(nPoints);

    Wmx.resize(nPoints);
    dWdt.resize(nPoints);
    dWdx.resize(nPoints);
    dWdtSplit.resize(nPoints);
}

void ConvectionSystemUTW::initialize()
{
    dTdtSplit.assign(nPoints, 0);
    dWdtSplit.assign(nPoints, 0);
}

void ConvectionSystemUTW::V2rV(void)
{
    V[0] = rV[0];
    if (alpha == 0) {
        for (size_t j=1; j<nPoints; j++) {
            rV[j] = V[j];
        }
    } else {
        for (size_t j=1; j<nPoints; j++) {
            rV[j] = x[j]*V[j];
        }
    }
}

void ConvectionSystemUTW::rV2V(void)
{
    V[0] = rV[0];
    if (alpha == 0) {
        for (size_t j=1; j<nPoints; j++) {
            V[j] = rV[j];
        }
    } else {
        for (size_t j=1; j<nPoints; j++) {
            V[j] = rV[j]/x[j];
        }
    }
}

int ConvectionSystemY::f(const realtype t, const sdVector& y, sdVector& ydot)
{
    assert (stopIndex-startIndex+1 == y.length());

    // *** Calculate v (= V/rho) ***
    update_v(t);

    // *** Calculate dY/dt

    // Left boundary conditions.
    // Convection term only contributes in the ControlVolume case
    if (startIndex == 0 && grid.leftBC == BoundaryCondition::ControlVolume) {
        double centerVol = pow(x[1],alpha+1) / (alpha+1);
        // Note: v[0] actually contains r*v[0] in this case
        double rvzero_mod = std::max(v[0], 0.0);
        ydot[0] = -rvzero_mod * (y[0] - Yleft) / centerVol;
    } else { // FixedValue, ZeroGradient, or truncated domain
        ydot[0] = 0;
    }

    // Intermediate points
    double dYdx;
    size_t i = 1;
    for (size_t j=startIndex+1; j<stopIndex; j++) {
        if (v[i] < 0) {
            dYdx = (y[i+1] - y[i]) / hh[j];
        } else {
            dYdx = (y[i] - y[i-1]) / hh[j-1];
        }
        ydot[i] = -v[i] * dYdx;
        i++;
    }

    // Right boundary values
    // Convection term has nothing to contribute in any case,
    // So only the value from the other terms remains
    ydot[i] = 0;

    return 0;
}

void ConvectionSystemY::get_diagonal(const realtype t, dvector& linearY)
{
    // Assume that f has just been called and that all auxiliary
    // arrays are in a consistent state.

    // Left boundary conditions.
    // Convection term only contributes in the ControlVolume case

    if (startIndex == 0 && grid.leftBC == BoundaryCondition::ControlVolume) {
        double centerVol = pow(x[1],alpha+1) / (alpha+1);
        // Note: v[0] actually contains r*v[0] in this case
        double rvzero_mod = std::max(v[0], 0.0);

        linearY[0] = -rvzero_mod / centerVol;

    } else { // FixedValue, ZeroGradient, or truncated domain
        linearY[0] = 0;
    }

    // Intermediate points
    size_t i = 1;
    for (size_t j=startIndex+1; j<stopIndex; j++) {
        // depends on upwinding to calculated dT/dx etc.
        linearY[i] = (v[i] < 0) ? v[i] / hh[j]
                                : -v[i] / hh[j-1];
        i++;
    }
    linearY[i] = 0;
}

int ConvectionSystemY::bandedJacobian(const realtype t, const sdVector& y,
                                     const sdVector& ydot, sdBandMatrix& J)
{
    // Assume that f has just been called and that all auxiliary
    // arrays are in a consistent state.

    // Left boundary condition.
    // Convection term only contributes in the ControlVolume case

    if (startIndex == 0 && grid.leftBC == BoundaryCondition::ControlVolume) {
        double centerVol = pow(x[1],alpha+1) / (alpha+1);
        double rvzero_mod = std::max(v[0], 0.0);

        J(0,0) = -rvzero_mod / centerVol;

    } else { // FixedValue, ZeroGradient or truncated domain
        J(0,0) = 0;
    }

    // Intermediate points
    size_t i = 1;
    for (size_t j=startIndex+1; j<stopIndex; j++) {
        // depends on upwinding to calculated dT/dx etc.
        double value = (v[i] < 0) ? v[i] / hh[j]
                                  : -v[i] / hh[j-1];
        J(i,i) = value;
        i++;
    }

    // Right boundary condition
    J(i,i) = 0;

    return 0;
}

void ConvectionSystemY::resize(const size_t new_nPoints)
{
    grid.setSize(new_nPoints);
    v.resize(nPoints);
}

void ConvectionSystemY::update_v(const double t)
{
    assert(vInterp->size() > 0);
    if (vInterp->size() == 1) {
        // vInterp only has one data point
        const dvector& vLeft = vInterp->begin()->second;
        size_t i = 0;
        for (size_t j=startIndex; j<=stopIndex; j++) {
            v[i] = vLeft[j];
            i++;
        }
        return;
    }

    // Find the value of v by interpolating between values contained in vInterp
    vecInterpolator::iterator iLeft = vInterp->lower_bound(t);
    if (iLeft == vInterp->end()) {
        // In this case, we're actually extrapolating past the rightmost point
        iLeft--;
    }
    vecInterpolator::iterator iRight = iLeft;
    iRight++;
    if (iRight == vInterp->end()) {
        iLeft--;
        iRight--;
    }
    const dvector& vLeft = iLeft->second;
    const dvector& vRight = iRight->second;

    // Linear interpolation
    double s = (t-iLeft->first)/(iRight->first - iLeft->first);

    size_t i = 0;
    for (size_t j=startIndex; j<=stopIndex; j++) {
        v[i] = vLeft[j]*(1-s) + vRight[j]*s;
        i++;
    }
}

ConvectionSystemSplit::ConvectionSystemSplit()
    : vInterp(new vecInterpolator())
    , nSpec(0)
    , nVars(3)
    , nPointsUTW(0)
    , startIndices(NULL)
    , stopIndices(NULL)
    , gas(NULL)
{
}

void ConvectionSystemSplit::get_diagonal
(const realtype t, dvector& dU, dvector& dT, Array2D& dY)
{
    // Called after evaluate()
    utwSystem.get_diagonal(t, dU, dT);

    for (size_t k=0; k<nSpec; k++) {
        dvector dYk(nPointsSpec[k], 0);
        speciesSystems[k].get_diagonal(t, dYk);
        size_t i = 0;
        for (size_t j=(*startIndices)[k]; j<(*stopIndices)[k]; j++) {
            dY(k,j) = dYk[i];
            i++;
        }
    }
}

void ConvectionSystemSplit::setGrid(const oneDimGrid& grid)
{
    GridBased::setGrid(grid);
    utwSystem.setGrid(grid);
    foreach (ConvectionSystemY& system, speciesSystems) {
        system.setGrid(grid);
    }
}

void ConvectionSystemSplit::setTolerances(const configOptions& options)
{
    reltol = options.integratorRelTol;
    abstolU = options.integratorMomentumAbsTol;
    abstolT = options.integratorEnergyAbsTol;
    abstolW = options.integratorSpeciesAbsTol * 20;
    abstolY = options.integratorSpeciesAbsTol;
}

void ConvectionSystemSplit::setGas(CanteraGas& gas_)
{
    gas = &gas_;
    utwSystem.gas = &gas_;
}

void ConvectionSystemSplit::resize
(const size_t nPointsUTWNew, const vector<size_t>& nPointsSpecNew, const size_t nSpecNew)
{
    // Create or destroy the necessary speciesSystems if nSpec has changed
    if (nSpec != nSpecNew) {
        speciesSystems.resize(nSpecNew);
        nSpec = nSpecNew;
        if (gas) {
            W.resize(nSpec);
            gas->getMolecularWeights(W);
        }
    }

    if (speciesSolvers.size() != nSpec) {
        // Create speciesSolvers from scratch if the number of species has changed
        speciesSolvers.clear();
        nPointsSpec = nPointsSpecNew;
        for (size_t k=0; k<nSpec; k++) {
            speciesSolvers.push_back(new sundialsCVODE(nPointsSpec[k]));
            configureSolver(speciesSolvers[k], k);
        }
    } else {
        // Replace the solvers where the number of points has changed
        for (size_t k=0; k<nSpec; k++) {
            if (nPointsSpec[k] != nPointsSpecNew[k]) {
                speciesSolvers.replace(k, new sundialsCVODE(nPointsSpecNew[k]));
                nPointsSpec[k] = nPointsSpecNew[k];
                configureSolver(speciesSolvers[k], k);
            }
        }
        nPointsSpec = nPointsSpecNew;
    }

    // Recreate the UTW solver if necessary
    if (nPointsUTW != nPointsUTWNew) {
        nPointsUTW = nPointsUTWNew;
        utwSolver.reset(new sundialsCVODE(3*nPointsUTW));
        utwSolver->setODE(&utwSystem);
        utwSolver->setBandwidth(0,0);
        utwSolver->reltol = reltol;
        utwSolver->linearMultistepMethod = CV_BDF;
        utwSolver->nonlinearSolverMethod = CV_NEWTON;
        for (size_t j=0; j<nPointsUTW; j++) {
            utwSolver->abstol[3*j+kMomentum] = abstolU;
            utwSolver->abstol[3*j+kEnergy] = abstolT;
            utwSolver->abstol[3*j+kWmx] = abstolW;
        }
        utwSystem.resize(nPointsUTW);
        Wmx.resize(nPointsUTW);
    }

    nPoints = nPointsUTWNew;
}

void ConvectionSystemSplit::setSpeciesDomains
(vector<size_t>& startIndices_, vector<size_t>& stopIndices_)
{
    startIndices = &startIndices_;
    stopIndices = &stopIndices_;
}

void ConvectionSystemSplit::setState(const dvector& U_, const dvector& T_, Array2D& Y_)
{
    U = U_;
    T = T_;
    Y = Y_;

    for (size_t j=0; j<nPointsUTW; j++) {
        utwSolver->y[3*j+kMomentum] = U[j];
        utwSolver->y[3*j+kEnergy] = T[j];
        gas->setStateMass(&Y(0,j), T[j]);
        utwSolver->y[3*j+kWmx] = Wmx[j] = gas->getMixtureMolecularWeight();
    }

    for (size_t k=0; k<nSpec; k++) {
        size_t i = 0;
        for (size_t j=(*startIndices)[k]; j<=(*stopIndices)[k]; j++) {
            speciesSolvers[k].y[i] = Y(k,j);
            i++;
        }
    }
}

void ConvectionSystemSplit::setLeftBC(const double Tleft, const dvector& Yleft_)
{
    utwSystem.Tleft = Tleft;
    Yleft = Yleft_;
}

void ConvectionSystemSplit::set_rVzero(const double rVzero)
{
    utwSystem.rVzero = rVzero;
}

void ConvectionSystemSplit::initialize(const double t0)
{
    // Initialize systems
    utwSystem.initialize();

    foreach (ConvectionSystemY& system, speciesSystems) {
        system.initialize();
    }

    // Set integration domain for each species
    for (size_t k=0; k<nSpec; k++) {
        speciesSystems[k].startIndex = (*startIndices)[k];
        speciesSystems[k].stopIndex = (*stopIndices)[k];
    }

    // Initialize solvers
    utwSolver->t0 = t0;
    utwSolver->maxNumSteps = 1000000;
    utwSolver->minStep = 1e-16;
    utwSolver->initialize();

    foreach (sundialsCVODE& solver, speciesSolvers) {
        solver.t0 = t0;
        solver.maxNumSteps = 1000000;
        solver.minStep = 1e-16;
        solver.initialize();
    }
}

void ConvectionSystemSplit::evaluate()
{
    sdVector ydotUTW(nVars*nPoints);
    utwSystem.f(utwSolver->tInt, utwSolver->y, ydotUTW);

    vInterp->clear();
    vInterp->insert(std::make_pair(utwSolver->tInt, utwSystem.V/utwSystem.rho));

    V = utwSystem.V;
    dUdt = utwSystem.dUdt;
    dTdt = utwSystem.dTdt;
    dWdt = utwSystem.dWdt;

    dYdt.data().clear();
    dYdt.resize(nSpec, nPoints, 0);
    sdVector ydotk(nPoints);
    for (size_t k=0; k<nSpec; k++) {
        speciesSystems[k].vInterp = vInterp;
        speciesSystems[k].f(speciesSolvers[k].tInt, speciesSolvers[k].y, ydotk);
        size_t i = 0;
        for (size_t j=(*startIndices)[k]; j<=(*stopIndices)[k]; j++) {
            dYdt(k,j) = ydotk[i];
            i++;
        }
    }
}

void ConvectionSystemSplit::setSplitDerivatives
(const dvector& dTdtSplit, const Array2D& dYdtSplit)
{
    utwSystem.dTdtSplit = dTdtSplit;

    for (size_t j=0; j<nPointsUTW; j++) {
        double value = 0;
        for (size_t k=0; k<nSpec; k++) {
             value += dYdtSplit(k,j)/W[k];
        }
        utwSystem.dWdtSplit[j] = - value * Wmx[j] * Wmx[j];
    }
}

void ConvectionSystemSplit::integrateToTime(const double tf)
{
    // Integrate the UTW system while storing the value of v after each timestep
    //boost::shared_ptr<vecInterpolator> vInterp(new vecInterpolator());
    utwTimer.start();
    vInterp->clear();

    sdVector ydotUTW(nVars*nPoints);
    utwSystem.f(utwSolver->tInt, utwSolver->y, ydotUTW);
    vInterp->insert(std::make_pair(utwSolver->tInt, utwSystem.V/utwSystem.rho));

    int cvode_flag = CV_SUCCESS;
    int i = 0;

    if (VERY_VERBOSE) {
        cout << "UTW...";
        cout.flush();
    }

    // CVODE returns CV_TSTOP_RETURN when the solver has reached tf
    while (cvode_flag != CV_TSTOP_RETURN) {
        cvode_flag = utwSolver->integrateOneStep(tf);
        i++;
        vInterp->insert(std::make_pair(utwSolver->tInt, utwSystem.V/utwSystem.rho));
    }

    utwTimer.stop();

    speciesTimer.start();
    if (VERY_VERBOSE) {
        cout << "Yk...";
        cout.flush();
    }
    // Integrate the species systems
    for (size_t k=0; k<nSpec; k++) {
        speciesSystems[k].vInterp = vInterp;
        speciesSolvers[k].integrateToTime(tf);
    }
    speciesTimer.stop();
}

int ConvectionSystemSplit::getNumSteps()
{
    int nSteps = utwSolver->getNumSteps();
    foreach (sundialsCVODE& solver, speciesSolvers) {
        nSteps += solver.getNumSteps();
    }
    return nSteps;
}

void ConvectionSystemSplit::unroll_y()
{
    for (size_t j=0; j<nPoints; j++) {
        T[j] = utwSolver->y[j*nVars+kEnergy];
        U[j] = utwSolver->y[j*nVars+kMomentum];
        Wmx[j] = utwSolver->y[j*nVars+kWmx];
    }

    for (size_t k=0; k<nSpec; k++) {
        size_t i = 0;
        for (size_t j=(*startIndices)[k]; j<=(*stopIndices)[k]; j++) {
            Y(k,j) = speciesSolvers[k].y[i];
            i++;
        }
    }
}

void ConvectionSystemSplit::configureSolver(sundialsCVODE& solver, const size_t k)
{
    solver.setODE(&speciesSystems[k]);
    solver.setBandwidth(0,0);
    solver.reltol = reltol;
    for (size_t j=0; j<nPointsSpec[k]; j++) {
        solver.abstol[j] = abstolY;
    }
    solver.linearMultistepMethod = CV_BDF;
    solver.nonlinearSolverMethod = CV_NEWTON;

    speciesSystems[k].resize(nPointsSpec[k]);
    speciesSystems[k].startIndex = (*startIndices)[k];
    speciesSystems[k].stopIndex = (*stopIndices)[k];
    speciesSystems[k].Yleft = Yleft[k];
    speciesSystems[k].k = k;
}
