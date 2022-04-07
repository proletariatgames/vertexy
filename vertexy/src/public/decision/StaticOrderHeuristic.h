// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once
#include "ConstraintTypes.h"
#include "ISolverDecisionHeuristic.h"
#include "ds/PriorityHeap.h"

namespace Vertexy
{
class ClauseConstraint;
class ConstraintSolver;

// Naive heuristic
class StaticOrderHeuristic : public ISolverDecisionHeuristic
{
protected:
	struct Comparator
	{
		vector<int>& priorities;

		Comparator(vector<int>& priorities)
			: priorities(priorities)
		{
		}

		bool operator()(uint32_t lhs, uint32_t rhs)
		{
			return priorities[lhs] > priorities[rhs];
		}
	};

	using VariableHeap = TPriorityHeap<uint32_t, Comparator>;

public:
	StaticOrderHeuristic(ConstraintSolver& solver);

	virtual void initialize() override;
	virtual bool getNextDecision(SolverDecisionLevel level, VarID& var, ValueSet& chosenValues) override;
	virtual void onVariableAssignment(VarID var, const ValueSet& prevValues, const ValueSet& newValues) override;
	virtual void onVariableUnassignment(VarID var, const ValueSet& beforeBacktrack, const ValueSet& afterBacktrack) override;

	virtual double getPriority(VarID id, int value) const override
	{
		vxy_assert(id.isValid());
		return m_priorities[id.raw()];
	}

protected:
	ConstraintSolver& m_solver;
	vector<int> m_priorities;
	// The priority queue for variable selection.
	VariableHeap m_heap;
};

} // namespace Vertexy