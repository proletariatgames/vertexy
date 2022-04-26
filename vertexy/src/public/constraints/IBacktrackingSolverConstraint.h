// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once

#include "ConstraintTypes.h"
#include "IConstraint.h"

namespace Vertexy
{

class ConstraintFactoryParams;

/** Interface for constraints that need to be notified whenever the solver backtracks */
class IBacktrackingSolverConstraint : public IConstraint
{
public:
	IBacktrackingSolverConstraint(const ConstraintFactoryParams& params)
		: IConstraint(params)
	{
	}

	virtual ~IBacktrackingSolverConstraint()
	{
	}

	virtual bool needsBacktracking() const override { return true; }

	// Called by the constraint solver if a contradiction has been reached and we need to backtrack.
	virtual void backtrack(const IVariableDatabase* db, SolverDecisionLevel level) = 0;
};

} // namespace Vertexy