// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once

#include "ConstraintTypes.h"
#include "constraints/ClauseConstraint.h"

namespace Vertexy
{
class ClauseConstraint;

// Interface for a policy that handles when a constraint solver restarts.
class IRestartPolicy
{
public:
	IRestartPolicy(const ConstraintSolver& solver)
		: m_solver(solver)
	{
	}

	virtual ~IRestartPolicy()
	{
	}

	/** Return true to indicate that the solver should restart. */
	virtual bool shouldRestart() = 0;
	/** Called by the solver whenever a new clause is learned */
	virtual void onClauseLearned(const ClauseConstraint& clause) = 0;
	/** Called by the solver whenever a restart happens (either triggered by us or not) */
	virtual void onRestarted() = 0;

protected:
	const ConstraintSolver& m_solver;
};

} // namespace Vertexy