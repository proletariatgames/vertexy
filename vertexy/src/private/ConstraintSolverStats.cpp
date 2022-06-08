// Copyright Proletariat, Inc. All Rights Reserved.
#include "ConstraintSolverStats.h"

#include "ConstraintSolver.h"
#include "util/TimeUtils.h"

using namespace Vertexy;

void ConstraintSolverStats::reset()
{
	startTime = 0;
	endTime = 0;
	stepCount = 0;
	numBacktracks = 0;
	maxBackjump = 0;
	numRestarts = 0;
	numInitialConstraints = 0;
	numConstraintsLearned = 0;
	numConstraintPromotions = 0;
	numFailedConstraintPromotions = 0;
	numGraphClonedConstraints = 0;
	numConstraintPurges = 0;
	numPurgedConstraints = 0;
	numLockedConstraintsToPurge = 0;
	numDuplicateLearnedConstraints = 0;
}

wstring ConstraintSolverStats::toString(bool verbose)
{
	wstring out;

	wstring status;
	switch (m_solver.getCurrentStatus())
	{
	case EConstraintSolverResult::Solved:
		status = TEXT("SAT");
		break;
	case EConstraintSolverResult::Unsatisfiable:
		status = TEXT("UNSAT");
		break;
	case EConstraintSolverResult::Unsolved:
		status = TEXT("Unsolved");
		break;
	case EConstraintSolverResult::Uninitialized:
		status = TEXT("Uninitialized");
		break;
	}

	out.append_sprintf(TEXT("\nSolver %s(%d): %s\n"), m_solver.getName().c_str(), m_solver.getSeed(), status.c_str());

	double duration = endTime > 0 ? (endTime - startTime) : (TimeUtils::getSeconds() - startTime);

	out.append_sprintf(TEXT("\tDuration: %.2fs\tIteration Count:%d\tBacktracks:%d\tRestarts:%d"),
		duration,
		stepCount,
		numBacktracks,
		numRestarts
	);

	if (verbose)
	{
		out.append_sprintf(TEXT("\n\tTight: %s"), nonTightRules ? TEXT("NO") : TEXT("YES"));
		out.append_sprintf(TEXT("\n\tNumber of variables: %d"), m_solver.getVariableDB()->getNumVariables());
		out.append_sprintf(TEXT("\n\tNumber of initial constraints: %d"), numInitialConstraints);
		out.append_sprintf(TEXT("\n\tNumber of learned constraints: %d"), numConstraintsLearned);
		out.append_sprintf(TEXT("\n\tLearned constraints purged: %d"), numPurgedConstraints);
		out.append_sprintf(TEXT("\n\tNumber of purges: %d"), numConstraintPurges);
		out.append_sprintf(TEXT("\n\tNumber of graph promotions: %d"), numConstraintPromotions);
		out.append_sprintf(TEXT("\n\tNumber of promotion failures: %d"), numFailedConstraintPromotions);
		out.append_sprintf(TEXT("\n\tNumber of constraints promoted from graphs: %d"), numGraphClonedConstraints);
		out.append_sprintf(TEXT("\n\tNumber of duplicate learned constraints: %d"), numDuplicateLearnedConstraints);
		out.append_sprintf(TEXT("\n\tLocked constraints during purge: %d"), numLockedConstraintsToPurge);
	}
	return out;
}
