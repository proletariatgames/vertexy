// Copyright Proletariat, Inc. All Rights Reserved.
#include "decision/LogOrderHeuristic.h"

#include "ConstraintSolver.h"
#include "util/SolverDecisionLog.h"

using namespace Vertexy;

void LogOrderHeuristic::initialize()
{
}

bool LogOrderHeuristic::getNextDecision(SolverDecisionLevel inLevel, VarID& var, ValueSet& chosenValues)
{
	if (m_nextDecision >= m_log->getNumDecisions())
	{
		VERTEXY_LOG("Finished replay");
		return false;
	}

	int curLevel = inLevel + m_levelOffset;

	if (m_log->getDecision(m_nextDecision).level < curLevel)
	{
		VERTEXY_LOG("Waiting to explore unexplored dead-end starting at %d (D=%d)", m_log->getDecision(m_nextDecision).level, m_nextDecision);
		return false;
	}

	// See if we're skipping over a branch previously taken
	int numToSkip = 0;
	while (m_log->getDecision(m_nextDecision + numToSkip).level > curLevel)
	{
		++numToSkip;
		vxy_assert_msg(m_nextDecision+numToSkip < m_log->getNumDecisions(), "Skipped over a solution found in log");
	}
	if (numToSkip > 0)
	{
		VERTEXY_LOG("Solver backtracked: Skipping over dead-end branch to Decision %d (%d skipped) level %d", m_nextDecision + numToSkip, numToSkip, m_log->getDecision(m_nextDecision + numToSkip).level);
		m_nextDecision += numToSkip;

		if (m_log->getDecision(m_nextDecision).level < curLevel)
		{
			VERTEXY_LOG("Waiting to explore unexplored dead-end starting at %d (D=%d)", m_log->getDecision(m_nextDecision).level, m_nextDecision);
			return false;
		}
	}

	// See if we've already ruled out a dead-end previously taken
	while (!m_solver.getVariableDB()->getPotentialValues(m_log->getDecision(m_nextDecision).variable)[m_log->getDecision(m_nextDecision).valueIndex])
	{
		int i;
		for (i = m_nextDecision + 1; i < m_log->getNumDecisions(); ++i)
		{
			if (m_log->getDecision(i).level <= curLevel)
			{
				break;
			}
		}
		vxy_assert_msg(i < m_log->getNumDecisions(), "Skipped solution found!");

		VERTEXY_LOG("Solver prevented assignment: Skipping over dead-end branch to Decision %d (%d skipped) level %d", i, i - m_nextDecision, m_log->getDecision(i).level);
		m_nextDecision = i;

		if (m_log->getDecision(m_nextDecision).level < curLevel)
		{
			VERTEXY_LOG("Waiting to explore unexplored dead-end starting at %d (D=%d)", m_log->getDecision(m_nextDecision).level, m_nextDecision);
			return false;
		}
	}

	while (true)
	{
		auto& potentialVals = m_solver.getVariableDB()->getPotentialValues(m_log->getDecision(m_nextDecision).variable);
		int solvedValue;
		if (!potentialVals.isSingleton(solvedValue))
		{
			break;
		}

		VERTEXY_LOG("Skipping already assigned: %s=%d", m_solver.getVariableName(m_log->getDecision(m_nextDecision).variable).c_str(), solvedValue);
		vxy_assert(solvedValue == m_log->getDecision(m_nextDecision).valueIndex);
		++m_nextDecision;
		++m_levelOffset;
		++curLevel;
		if (m_nextDecision >= m_log->getNumDecisions())
		{
			VERTEXY_LOG("Finished replay");
			return false;
		}

		if (m_log->getDecision(m_nextDecision).level < curLevel)
		{
			VERTEXY_LOG("Waiting to explore unexplored dead-end starting at %d (D=%d)", m_log->getDecision(m_nextDecision).level, m_nextDecision);
			return false;
		}
	}

	vxy_sanity(m_log->getDecision(m_nextDecision).level == curLevel);

	var = m_log->getDecision(m_nextDecision).variable;
	chosenValues.init(m_solver.getDomain(var).getDomainSize(), false);
	chosenValues[m_log->getDecision(m_nextDecision).valueIndex] = true;

	++m_nextDecision;
	return true;
}
