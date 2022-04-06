// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once
#include "ConstraintTypes.h"

namespace csolver
{

/** Common interface for solver decision strategies, i.e. what variable/value is chosen next while searching. */
class ISolverDecisionHeuristic
{
public:
	virtual ~ISolverDecisionHeuristic()
	{
	}

	// Initialize. Called before starting search.
	virtual void initialize()
	{
	}

	// Return the next decision: the variable and value to assign.
	// Postcondition: if true is returned the value returned should both be possible and narrow the variable's current
	// potential values.
	// If false is returned, the next decision heuristic on the stack is consulted. If there are no more heuristics,
	// then false indicates that all variables have been solved.
	virtual bool getNextDecision(SolverDecisionLevel level, VarID& var, ValueSet& chosenValues) = 0;
	// Called every time a variable changes due to a decision or propagation
	virtual void onVariableAssignment(VarID var, const ValueSet& prevValues, const ValueSet& newValues)
	{
	}

	// Called during backtracking whenever a previously assigned/propagated variable change is un-done
	virtual void onVariableUnassignment(VarID var, const ValueSet& beforeBacktrack, const ValueSet& afterBacktrack)
	{
	}

	// Called for every variable that is in a learned clause during conflict analysis
	virtual void onVariableConflictActivity(VarID var, const ValueSet& values, const ValueSet& prevValues)
	{
	}

	// Called for every variable that is on the reason (left) side of the UIP during conflict analysis
	virtual void onVariableReasonActivity(VarID var, const ValueSet& values, const ValueSet& prevValues)
	{
	}

	// Whether we want reason activity. It costs more during conflict analysis, so should be skipped unless the information is useful.
	virtual bool wantsReasonActivity() const { return false; }
	// Called after a search dead-end is reached, after a clause is learned
	virtual void onClauseLearned()
	{
	}

	// Called whenever the solver restarts search from scratch
	virtual void onRestarted()
	{
	}

	// Return the priority of the given variable+value
	virtual double getPriority(VarID varID, int value) const { return 0.0; }
};

} // namespace csolver