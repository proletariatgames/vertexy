// Copyright Proletariat, Inc. All Rights Reserved.

#include "restart/LubyRestartPolicy.h"

#include "ConstraintSolver.h"

using namespace Vertexy;

static constexpr int INITIAL_NUM_CONFLICTS_FOR_RESTART = 100;
static constexpr float GROWTH_NUM_CONFLICTS_FOR_RESTART = 2.0f;

LubyRestartPolicy::LubyRestartPolicy(const ConstraintSolver& solver)
	: IRestartPolicy(solver)
	, m_maxConflictsBeforeRestart(INITIAL_NUM_CONFLICTS_FOR_RESTART)
{
}

bool LubyRestartPolicy::shouldRestart()
{
	return m_restartConflictCounter >= m_maxConflictsBeforeRestart;
}

void LubyRestartPolicy::onClauseLearned(const ClauseConstraint&)
{
	++m_restartConflictCounter;
}

void LubyRestartPolicy::onRestarted()
{
	if (m_restartConflictCounter >= m_maxConflictsBeforeRestart)
	{
		m_maxConflictsBeforeRestart = INITIAL_NUM_CONFLICTS_FOR_RESTART * luby(GROWTH_NUM_CONFLICTS_FOR_RESTART, m_solver.getStats().numRestarts);
		//VERTEXY_LOG("%d conflicts reached, restarting with %d max conflicts...", RestartConflictCounter, MaxConflictsBeforeRestart);
	}
	m_restartConflictCounter = 0;
}

/**
 *
 * Basically, this is an infinite sequence of the pattern:
 * 1 1 2
 * 1 1 2   1 1 2 4
 * 1 1 2   1 1 2 4  1 1 2
 * 1 1 2   1 1 2 4  1 1 2 4  1 1 2 4 8
 * ...
 *
 *
 */
/*static*/
float LubyRestartPolicy::luby(float y, int x)
{
	int size, seq;
	for (size = 1, seq = 0; size < x + 1; seq++, size = 2 * size + 1)
	{
	}

	while (size - 1 != x)
	{
		size = (size - 1) >> 1;
		seq--;
		x = x % size;
	}

	return powf(y, seq);
}
