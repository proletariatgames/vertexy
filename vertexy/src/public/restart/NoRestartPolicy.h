// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once

#include "ConstraintTypes.h"
#include "IRestartPolicy.h"

namespace Vertexy
{

/** Stub restart policy that never restarts */
class NoRestartPolicy : public IRestartPolicy
{
public:
	NoRestartPolicy(const ConstraintSolver& solver)
		: IRestartPolicy(solver)
	{
	}

	virtual bool shouldRestart() override { return false; }

	virtual void onClauseLearned(const ClauseConstraint& learnedClause) override
	{
	}

	virtual void onRestarted() override
	{
	}
};

} // namespace Vertexy