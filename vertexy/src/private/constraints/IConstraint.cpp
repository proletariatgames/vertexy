// Copyright Proletariat, Inc. All Rights Reserved.
#include "constraints/IConstraint.h"
#include "ConstraintSolver.h"

using namespace Vertexy;

vector<Literal> IConstraint::explain(const NarrowingExplanationParams& params) const
{
    // Find all dependent variables for this constraint that were previously narrowed, and add their (inverted) value to the list.
    // The clause will look like:
    // (Arg1 != Arg1Values OR Arg2 != Arg2Values OR [...] OR PropagatedVariable == PropagatedValues)
    const vector<VarID>& constraintVars = params.solver->getVariablesForConstraint(params.constraint);
    vector<Literal> clauses;
    clauses.reserve(constraintVars.size());

    bool foundPropagated = false;
    for (int i = 0; i < constraintVars.size(); ++i)
    {
        VarID arg = constraintVars[i];
        clauses.push_back(Literal(arg, params.database->getPotentialValues(arg)));
        clauses.back().values.invert();

        if (arg == params.propagatedVariable)
        {
            foundPropagated = true;
            clauses.back().values.pad(params.propagatedValues.size(), false);
            clauses.back().values.include(params.propagatedValues);
        }
    }

    vxy_assert(foundPropagated || params.propagatedVariable == VarID::INVALID);
    return clauses;
}