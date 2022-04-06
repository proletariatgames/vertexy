// Copyright Proletariat, Inc. All Rights Reserved.
#include "decision/CoarseLRBHeuristic.h"

#include "ConstraintSolver.h"
#include "variable/IVariableDatabase.h"

using namespace csolver;

static constexpr float MIN_STEP_SIZE = 0.06f;
static constexpr float STEP_DECAY_SIZE = 10e-6f;
static constexpr float RECENCY_DECAY = 0.99f;
static constexpr float EMA_SEED_RANGE = 0.75f;
static constexpr bool USE_REASON_ACTIVITY = false;

CoarseLRBHeuristic::CoarseLRBHeuristic(ConstraintSolver& solver)
	: m_solver(solver)
	, m_heap(Comparator(m_priorities))
	, m_wantReasonActivity(USE_REASON_ACTIVITY)
	, m_stepSize(0.4)
	, m_learntCounter(0)
{
}

void CoarseLRBHeuristic::initialize()
{
	auto db = m_solver.getVariableDB();
	int numVars = db->getNumVariables();

	m_heap.reserve(numVars + 1);
	m_priorities.resize(numVars + 1, 0);

	m_assigned.resize(numVars + 1, 0);
	m_unassigned.resize(numVars + 1, 0);
	m_participated.resize(numVars + 1, 0);
	m_reasoned.resize(numVars + 1, 0);

	for (int i = 1; i < numVars + 1; ++i)
	{
		if (!db->isSolved(VarID(i)))
		{
			// Initialize with random values so seed actually matters
			// Over time, the LRB heuristic will become more prominent
			m_priorities[i] = m_solver.randomRangeFloat(0.f, EMA_SEED_RANGE);
			m_heap.insert(i);
		}
	}
}

bool CoarseLRBHeuristic::getNextDecision(SolverDecisionLevel level, VarID& var, ValueSet& chosenValues)
{
	auto db = m_solver.getVariableDB();
	if (m_heap.empty())
	{
		return false;
	}

	uint32_t heapValue = m_heap.peek();
	cs_assert(heapValue > 0);

	uint32_t age = m_learntCounter - m_unassigned[heapValue];
	while (age > 0)
	{
		float decay = powf(RECENCY_DECAY, age);
		m_priorities[heapValue] *= decay;
		m_heap.update(heapValue);
		m_unassigned[heapValue] = m_learntCounter;

		heapValue = m_heap.peek();
		age = m_learntCounter - m_unassigned[heapValue];
	}

	var = VarID(heapValue);
	cs_assert(var.isValid());

	const ValueSet& potentials = db->getPotentialValues(var);

	int value;
	if (m_solver.getVariableDB()->getLastSolvedValue(var, value) && potentials[value])
	{
		//CS_LOG("Picking prev value %d = %d", Var, Value);
	}
	else
	{
		// pick a random bit to set
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

	chosenValues.pad(db->getDomainSize(var), false);
	chosenValues[value] = true;

	return true;
}

void CoarseLRBHeuristic::onVariableAssignment(VarID var, const ValueSet& prevValues, const ValueSet& newValues)
{
	if (newValues.isSingleton())
	{
		m_assigned[var.raw()] = m_learntCounter;
		m_participated[var.raw()] = 0;
		m_reasoned[var.raw()] = 0;
		m_heap.remove(var.raw());
	}
}

void CoarseLRBHeuristic::onVariableUnassignment(VarID var, const ValueSet& beforeBacktrack, const ValueSet& afterBacktrack)
{
	if (beforeBacktrack.isSingleton())
	{
		if (!m_heap.inHeap(var.raw()))
		{
			const float interval = float(m_learntCounter - m_assigned[var.raw()]);
			if (interval > 0)
			{
				const float r = m_participated[var.raw()] / interval; // Learning rate
				const float rsr = m_reasoned[var.raw()] / interval; // Reason side rate
				m_priorities[var.raw()] = (1.0f - m_stepSize) * m_priorities[var.raw()] + m_stepSize * (r + rsr);
			}

			m_heap.insert(var.raw());
		}
		m_unassigned[var.raw()] = m_learntCounter;
	}
}

void CoarseLRBHeuristic::onVariableConflictActivity(VarID var, const ValueSet& values, const ValueSet& prevValues)
{
	m_participated[var.raw()]++;
}

void CoarseLRBHeuristic::onVariableReasonActivity(VarID var, const ValueSet& values, const ValueSet& prevValues)
{
	m_reasoned[var.raw()]++;
}

void CoarseLRBHeuristic::onClauseLearned()
{
	++m_learntCounter;
	m_stepSize = max(MIN_STEP_SIZE, m_stepSize - STEP_DECAY_SIZE);
}
