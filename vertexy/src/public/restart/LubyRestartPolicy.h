// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once

#include "ConstraintTypes.h"
#include "IRestartPolicy.h"

namespace Vertexy
{

/*
 * Luby heuristic for restarting the solver.
 * See "Optimal Speedup of Las Vegas Algorithms, Luby et. al.
 * https://www.cs.utexas.edu/~diz/Sub%20Websites/Research/optimal_speedup_of_las_vegas_algorithms.pdf
 */
class LubyRestartPolicy final : public IRestartPolicy
{
public:
	LubyRestartPolicy(const ConstraintSolver& solver);

	virtual bool shouldRestart() override;
	virtual void onClauseLearned(const ClauseConstraint& learnedClause) override;
	virtual void onRestarted() override;

protected:
	// Maximum number of conflicts before we restart
	int m_maxConflictsBeforeRestart;

	// Number of conflicts since we last restarted
	int m_restartConflictCounter = 0;

	static float luby(float y, int x);
};

} // namespace Vertexy