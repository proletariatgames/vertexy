// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once

#include "ConstraintTypes.h"

namespace csolver
{
class ConstraintSolverStats
{
public:
	explicit ConstraintSolverStats(ConstraintSolver& solver)
		: m_solver(solver)
	{
	}

	void reset();
	wstring toString(bool verbose = false);

	// Solving start time
	double startTime = 0;
	// Solving end time
	double endTime = 0;
	// How many steps have been performed
	uint32_t stepCount = 0;
	// How many times we had to backtrack
	uint32_t numBacktracks = 0;
	// The maximum backjump we had to do
	uint32_t maxBackjump = 0;
	// Number of times we've restarted
	uint32_t numRestarts = 0;
	// How many initial constraints existed
	uint32_t numInitialConstraints = 0;
	// How many constraints were learned (including those that were purged)
	uint32_t numConstraintsLearned = 0;
	// How many learned constraints have been promoted to graph constraints
	uint32_t numConstraintPromotions = 0;
	// How many promotions failed to generate any constraints;
	uint32_t numFailedConstraintPromotions = 0;
	// The number of constraints generated from promoted constraints
	uint32_t numGraphClonedConstraints = 0;
	// Number of times we've purged learned constraint db
	uint32_t numConstraintPurges = 0;
	// Number of learned constraints purged
	uint64_t numPurgedConstraints = 0;
	// Number of times a constraint was not purged because it was locked
	uint64_t numLockedConstraintsToPurge = 0;
	// Number of duplicate learned constraints found. (Only valid after solver finishes with solution)
	uint64_t numDuplicateLearnedConstraints = 0;

protected:
	ConstraintSolver& m_solver;
};

} // namespace csolver