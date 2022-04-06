// Copyright Proletariat, Inc. All Rights Reserved.
#include "restart/LBDRestartPolicy.h"

#include "ConstraintSolver.h"

using namespace csolver;

// Number of samples to store for tracking average LBD of learned clauses
static constexpr uint32_t LBD_QUEUE_SIZE = 50;
// Number of samples to store of tracking average of assignment stack length
static constexpr uint32_t ASSIGNMENT_QUEUE_SIZE = 5000;
// The minimum number of conflicts we must reach before we allow blocking restarts for out-of-band depths
static constexpr uint32_t LOWER_BOUND_FOR_BLOCKING_RESTART = 10000;
// Multiplier for average LBD of learned clauses. Increasing this will cause restarts to happen more frequently.
static constexpr float LBD_QUEUE_SCALE = 0.8f;
// Multiplier for average assignment queue check. Decreasing this will block restarts from occurring at out-of-band depths more frequently.
static constexpr float ASSIGNMENT_QUEUE_SCALE = 1.2f;

LBDRestartPolicy::LBDRestartPolicy(const ConstraintSolver& solver)
	: IRestartPolicy(solver)
	, m_assignmentStackSizeQueue(ASSIGNMENT_QUEUE_SIZE)
	, m_lbdSizeQueue(LBD_QUEUE_SIZE)
{
}

bool LBDRestartPolicy::shouldRestart()
{
	uint32_t stackSize = m_solver.getVariableDB()->getAssignmentStack().getStack().size();

	// Don't restart if our current search depth is over the current average (times a constant)
	if (m_conflictCounter > LOWER_BOUND_FOR_BLOCKING_RESTART && m_lbdSizeQueue.atCapacity() &&
		((m_assignmentStackSizeQueue.getFloatAverage() * ASSIGNMENT_QUEUE_SCALE) < stackSize))
	{
		m_lbdSizeQueue.clear();
	}

	// Restart if our average LBD in the queue (times a constant) is more than our overall average LBD rate
	// (High LBD means potential conflicts could cause lots of backtracking)
	const float lbdRate = m_lbdTotal / float(m_conflictCounter);
	if (m_lbdSizeQueue.atCapacity() && (m_lbdSizeQueue.getFloatAverage() * LBD_QUEUE_SCALE) > lbdRate)
	{
		//CS_LOG("**Restarting: LBD Average = %f, LBD rate = %f...", LBDSizeQueue.GetFloatAverage(), LBDRate);
		return true;
	}

	return false;
}

void LBDRestartPolicy::onRestarted()
{
	m_lbdSizeQueue.clear();
}

void LBDRestartPolicy::onClauseLearned(const ClauseConstraint& learnedClause)
{
	uint32_t lbd = learnedClause.getLBD();

	m_lbdTotal += lbd;
	m_lbdSizeQueue.pushBack(lbd);
	++m_conflictCounter;

	m_assignmentStackSizeQueue.pushBack(m_solver.getVariableDB()->getAssignmentStack().getStack().size());
}
