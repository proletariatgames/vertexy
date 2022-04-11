// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once

#include "ConstraintTypes.h"

namespace Vertexy
{

class IVariableDatabase;
class IConstraint;

// Represents current running state of the solver: what decisions have been made, and what changes have been
// propagated. Allows for backtracking.
class AssignmentStack
{
public:
	using Timestamp = int;

	static constexpr Timestamp TIMESTAMP_INITIAL = -1;

	struct Modification
	{
		VarID variable;
		ValueSet previousValue;
		Timestamp previousVariableAssignment;
		IConstraint* constraint;
		ExplainerFunction explanation;
	};

	using BacktrackCallback = function<void(const Modification&)>;

	AssignmentStack();

	// Reset the stack, removing all entries.
	void reset();

	/*** Record a change (narrowing of scope) to a variable. */
	SolverTimestamp recordChange(VarID variable, const ValueSet& prevValues, SolverTimestamp previousModificationTS, IConstraint* constraint, ExplainerFunction explanation);

	inline const vector<Modification>& getStack() const { return m_stack; }

	/** Get the modification at the given timestamp */
	inline const Modification& getModificationAtTime(SolverTimestamp stamp) const
	{
		return m_stack[stamp];
	}

	/** Get the most recent timestamp. NOTE will not be valid before PrepareForSolving is called! */
	SolverTimestamp getMostRecentTimestamp() const { return m_stack.size() - 1; }

	/**
	 *  Backtracks the stack to the given timestamp, calling the specified callback every time a variable
	 *  change is removed.
	 */
	void backtrackToTime(SolverTimestamp timestamp, const BacktrackCallback& callback);

protected:
	vector<Modification> m_stack;
};

} // namespace Vertexy