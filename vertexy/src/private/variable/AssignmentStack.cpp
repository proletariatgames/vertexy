// Copyright Proletariat, Inc. All Rights Reserved.
#include "variable/AssignmentStack.h"
#include "variable/IVariableDatabase.h"

using namespace Vertexy;

AssignmentStack::AssignmentStack()
{
}

void AssignmentStack::reset()
{
	m_stack.clear();
}

SolverTimestamp AssignmentStack::recordChange(VarID variable, const ValueSet& prevValues, SolverTimestamp previousModificationTS, IConstraint* constraint, ExplainerFunction explanation)
{
	SolverTimestamp time = m_stack.size();
	m_stack.push_back({variable, prevValues, previousModificationTS, constraint, move(explanation)});
	return time;
}

void AssignmentStack::backtrackToTime(SolverTimestamp time, const BacktrackCallback& callback)
{
	while (getMostRecentTimestamp() > time)
	{
		const Modification& top = m_stack.back();
		callback(top);
		m_stack.pop_back();
	}
}
