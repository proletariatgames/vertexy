// Copyright Proletariat, Inc. All Rights Reserved.
#include "decision/VSIDSHeuristic.h"

#include "ConstraintSolver.h"

using namespace csolver;

static constexpr double INITIAL_ACTIVITY_RANGE = 1.0;
static constexpr double MAX_ACTIVITY = 1e100;
static constexpr double ACTIVITY_RESCALE = 1e-100;
static constexpr double INITIAL_DECAY_AMOUNT = 1.0 / 0.85;
static constexpr double MAX_DECAY_AMOUNT = 1.0 / 0.999;
static constexpr double DECAY_STEP = 0.01;
static constexpr int DECAY_UPDATE_FREQUENCY = 5000;
static constexpr float RANDOM_CHANCE = 0.f;

VSIDSHeuristic::VSIDSHeuristic(ConstraintSolver& solver)
	: m_solver(solver)
	, m_heap(Comparator(m_priorities))
	, m_increment(1.0)
	, m_decay(INITIAL_DECAY_AMOUNT)
{
}

void VSIDSHeuristic::initialize()
{
	const int numVars = m_solver.getVariableDB()->getNumVariables();
	m_heap.reserve(numVars + 1);

	m_priorities.resize(numVars + 1, 0);
	for (int i = 1; i < numVars + 1; ++i)
	{
		if (!m_solver.getVariableDB()->isSolved(VarID(i)))
		{
			m_priorities[i] = m_solver.randomRangeFloat(0.0, INITIAL_ACTIVITY_RANGE);
			m_heap.insert(i);
		}
	}
}

bool VSIDSHeuristic::getNextDecision(SolverDecisionLevel level, VarID& varID, ValueSet& chosenValues)
{
	auto db = m_solver.getVariableDB();

	if (m_heap.empty())
	{
		return false;
	}

	if (m_solver.randomFloat<float>() < RANDOM_CHANCE)
	{
		int randomIndex = m_solver.randomRange(0, m_heap.size() - 1);
		varID = VarID(m_heap[randomIndex]);
	}
	else
	{
		varID = VarID(m_heap.peek());
	}

	const ValueSet& potentials = db->getPotentialValues(varID);

	int value;
	if (m_solver.getVariableDB()->getLastSolvedValue(varID, value) && potentials[value])
	{
		//CS_LOG("Picking prev value %d = %d", Var, Value);
	}
	else
	{
		//Value = Potentials.Find(true);

		const int numVals = potentials.getNumSetBits();
		const int randomIndex = m_solver.randomRange(0, numVals - 1);

		auto it = potentials.beginSetBits();
		for (int i = 0; i < randomIndex; ++i)
		{
			++it;
			++i;
		}

		value = *it;
	}

	chosenValues.pad(db->getDomainSize(varID), false);
	chosenValues[value] = true;

	return true;
}

void VSIDSHeuristic::onVariableAssignment(VarID varID, const ValueSet& prevValues, const ValueSet& newValues)
{
	if (newValues.isSingleton())
	{
		m_heap.remove(varID.raw());
	}
}

void VSIDSHeuristic::onVariableUnassignment(VarID varID, const ValueSet& beforeBacktrack, const ValueSet& afterBacktrack)
{
	if (beforeBacktrack.isSingleton())
	{
		m_heap.insert(varID.raw());
	}
}

void VSIDSHeuristic::onVariableConflictActivity(VarID varID, const ValueSet& values, const ValueSet& prevValues)
{
	increasePriority(varID, m_increment);
}

void VSIDSHeuristic::increasePriority(VarID varID, double increment)
{
	cs_assert(varID.isValid());

	m_priorities[varID.raw()] += increment;
	if (m_priorities[varID.raw()] > MAX_ACTIVITY)
	{
		const int numVar = m_solver.getVariableDB()->getNumVariables();
		for (int i = 1; i < numVar + 1; ++i)
		{
			m_priorities[i] *= ACTIVITY_RESCALE;
		}
		m_increment *= ACTIVITY_RESCALE;
	}

	if (m_heap.inHeap(varID.raw()))
	{
		m_heap.update(varID.raw());
	}
}

void VSIDSHeuristic::onClauseLearned()
{
	++m_numConflicts;
	m_increment *= m_decay;
	if ((m_numConflicts % DECAY_UPDATE_FREQUENCY) == 0)
	{
		m_decay = max(MAX_DECAY_AMOUNT, m_decay - DECAY_STEP);
	}
}
