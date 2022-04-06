// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once

#include "ConstraintTypes.h"
#include "ISolverConstraint.h"

namespace csolver
{

class ConstraintFactoryParams;

/** Interface for constraints that need to be notified whenever the solver backtracks */
class IBacktrackingSolverConstraint : public ISolverConstraint
{
public:
	IBacktrackingSolverConstraint(const ConstraintFactoryParams& params)
		: ISolverConstraint(params)
	{
	}

	virtual ~IBacktrackingSolverConstraint()
	{
	}

	virtual bool needsBacktracking() const override { return true; }

	// Called by the constraint solver if a contradiction has been reached and we need to backtrack.
	virtual void backtrack(const IVariableDatabase* db, SolverDecisionLevel level) = 0;
};

} // namespace csolver