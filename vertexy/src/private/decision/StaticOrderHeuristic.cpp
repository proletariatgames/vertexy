// Copyright Proletariat, Inc. All Rights Reserved.
#include "decision/StaticOrderHeuristic.h"

#include "ConstraintSolver.h"

using namespace Vertexy;

StaticOrderHeuristic::StaticOrderHeuristic(ConstraintSolver& solver)
	: m_solver(solver)
	, m_heap(Comparator(m_priorities))
{
}

void StaticOrderHeuristic::initialize()
{
	const int numVars = m_solver.getVariableDB()->getNumVariables();
	m_heap.reserve(numVars + 1);
	m_priorities.resize(numVars + 1, 0);

	int priority = 0;
	for (int i = 1; i < numVars + 1; ++i)
	{
		if (!m_solver.getVariableDB()->isSolved(VarID(i)))
		{
			m_priorities[i] = priority++;
			m_heap.insert(i);
		}
	}
}

bool StaticOrderHeuristic::getNextDecision(SolverDecisionLevel level, VarID& var, ValueSet& chosenValues)
{
	auto db = m_solver.getVariableDB();

	if (m_heap.empty())
	{
		return false;
	}

	var = VarID(m_heap.peek());

	const ValueSet& potentials = db->getPotentialValues(var);

	int value;
	if (!m_solver.getVariableDB()->getLastSolvedValue(var, value) || !potentials[value])
	{
		value = potentials.indexOf(true);
	}

	chosenValues.pad(db->getDomainSize(var), false);
	chosenValues[value] = true;

	return true;
}

void StaticOrderHeuristic::onVariableAssignment(VarID var, const ValueSet& prevValues, const ValueSet& newValues)
{
	if (newValues.isSingleton())
	{
		m_heap.remove(var.raw());
	}
}

void StaticOrderHeuristic::onVariableUnassignment(VarID var, const ValueSet& beforeBacktrack, const ValueSet& afterBacktrack)
{
	if (beforeBacktrack.isSingleton())
	{
		m_heap.insert(var.raw());
	}
}
