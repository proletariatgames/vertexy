// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once
#include "ConstraintTypes.h"
#include "ISolverDecisionHeuristic.h"
#include "ds/PriorityHeap.h"

namespace Vertexy
{

class ClauseConstraint;
class ConstraintSolver;

// Standard VSIDS Heuristic
class VSIDSHeuristic : public ISolverDecisionHeuristic
{
protected:
	struct Comparator
	{
		vector<double>& priorities;

		Comparator(vector<double>& priorities)
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
	VSIDSHeuristic(ConstraintSolver& solver);

	virtual void initialize() override;
	virtual bool getNextDecision(SolverDecisionLevel level, VarID& varID, ValueSet& chosenValues) override;

	// Called every time a variable changes due to a decision or propagation
	virtual void onVariableAssignment(VarID varID, const ValueSet& prevValues, const ValueSet& newValues) override;
	// Called during backtracking whenever a previously assigned/propagated variable change is un-done
	virtual void onVariableUnassignment(VarID varID, const ValueSet& beforeBacktrack, const ValueSet& afterBacktrack) override;
	// Called for every variable that is in a learned clause during conflict analysis
	virtual void onVariableConflictActivity(VarID varID, const ValueSet& values, const ValueSet& prevValues) override;

	// Called after a search dead-end is reached, after a clause is learned
	virtual void onClauseLearned() override;

	virtual void onRestarted() override { ++m_restartCounter; }

	// Return the priority of the given variable+value
	virtual double getPriority(VarID varID, int value) const override
	{
		vxy_assert(varID.isValid());
		return m_priorities[varID.raw()];
	}

protected:
	void increasePriority(VarID varID, double increment);

	ConstraintSolver& m_solver;

	vector<double> m_priorities;
	// The priority queue for variable selection.
	VariableHeap m_heap;

	double m_increment;
	double m_decay;
	int m_numConflicts = 0;
	int m_restartCounter = 0;
};

} // namespace Vertexy