// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once
#include "ConstraintTypes.h"
#include "ISolverDecisionHeuristic.h"
#include "ds/PriorityHeap.h"

namespace Vertexy
{

class ClauseConstraint;
class ConstraintSolver;

// Learning-Rate-Based heuristic for choosing variables/values
// See https://cs.uwaterloo.ca/~ppoupart/publications/sat/learning-rate-branching-heuristic-SAT.pdf
// This version is coarse: it only tracks variables, not individual values
class CoarseLRBHeuristic : public ISolverDecisionHeuristic
{
protected:
	struct Comparator
	{
		vector<float>& priorities;

		Comparator(vector<float>& priorities)
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
	CoarseLRBHeuristic(ConstraintSolver& solver);

	virtual void initialize() override;
	virtual bool getNextDecision(SolverDecisionLevel level, VarID& var, ValueSet& chosenValues) override;

	// Called every time a variable changes due to a decision or propagation
	virtual void onVariableAssignment(VarID var, const ValueSet& prevValues, const ValueSet& newValues) override;
	// Called during backtracking whenever a previously assigned/propagated variable change is un-done
	virtual void onVariableUnassignment(VarID var, const ValueSet& beforeBacktrack, const ValueSet& afterBacktrack) override;

	// Called for every variable that is in a learned clause during conflict analysis
	virtual void onVariableConflictActivity(VarID var, const ValueSet& values, const ValueSet& prevValues) override;
	// Called for every variable that is in the reason for a conflict.
	virtual void onVariableReasonActivity(VarID var, const ValueSet& values, const ValueSet& prevValues) override;

	virtual bool wantsReasonActivity() const override { return m_wantReasonActivity; }

	// Called after a search dead-end is reached, after a clause is learned
	virtual void onClauseLearned() override;

	// Return the priority of the given variable+value
	virtual double getPriority(VarID varID, int value) const override
	{
		return m_priorities[varID.raw()];
	}

protected:
	ConstraintSolver& m_solver;

	vector<float> m_priorities;

	// The priority queue for variable/value selection.
	// Each value in the queue refers to a unique variable+value.
	// See KeyOffsets/KeyToVar
	VariableHeap m_heap;

	// Whether we want to leverage reason activity. Potentially improves the heuristic, but fairly
	// expensive to calculate (depending on constraints used)
	bool m_wantReasonActivity;

	// Controls weights for the exponential moving average (EMA)
	// Higher values weight historical data over recent data
	// Must be between 0-1 (exclusive)
	float m_stepSize;
	// Incremented each time we reach a dead-end (and hence learn a new clause)
	int m_learntCounter;

	//
	// The remainder of these are sized to include every potential value of every variable.
	//

	// How many clauses were learned at the last point this variable was backtracked from
	vector<uint32_t> m_unassigned;
	// How many clauses were learned at the point this variable+value was assigned
	vector<uint32_t> m_assigned;
	// Count of how many times this variable+value was involved in a conflict since it was last assigned
	vector<uint32_t> m_participated;
	// Count of how many times this variable+value was on the "reason side" of the implication graph in a conflict,
	// since it was last assigned
	vector<uint32_t> m_reasoned;
};

} // namespace Vertexy