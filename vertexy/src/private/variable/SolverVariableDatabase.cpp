// Copyright Proletariat, Inc. All Rights Reserved.
#include "variable/SolverVariableDatabase.h"

#include "ConstraintSolver.h"
#include "SignedClause.h"
#include "constraints/ClauseConstraint.h"
#include "constraints/ISolverConstraint.h"

using namespace Vertexy;

//
// Default explanation function for a violated constraint. This will return a true explanation, but is not necessarily
// the smallest explanation possible, especially for constraints involving more than 2 variables.
//
vector<Literal> IVariableDatabase::defaultExplainer(const NarrowingExplanationParams& params)
{
	// Find all dependent variables for this constraint that were previously narrowed, and add their (inverted) value to the list.
	// The clause will look like:
	// (Arg1 != Arg1Values OR Arg2 != Arg2Values OR [...] OR PropagatedVariable == PropagatedValues)
	const vector<VarID>& constraintVars = params.solver->getVariablesForConstraint(params.constraint);
	vector<Literal> clauses;
	clauses.reserve(constraintVars.size());

	bool foundPropagated = false;
	for (int i = 0; i < constraintVars.size(); ++i)
	{
		VarID arg = constraintVars[i];
		clauses.push_back(Literal(arg, params.database->getPotentialValues(arg)));
		clauses.back().values.invert();

		if (arg == params.propagatedVariable)
		{
			foundPropagated = true;
			clauses.back().values.pad(params.propagatedValues.size(), false);
			clauses.back().values.include(params.propagatedValues);
		}
	}

	vxy_assert(foundPropagated);
	return clauses;
}

int IVariableDatabase::getMinimumPossibleDomainValue(VarID varID) const
{
	vxy_assert(varID.isValid());
	return getSolver()->getDomain(varID).getValueForIndex(getMinimumPossibleValue(varID));
}

int IVariableDatabase::getMaximumPossibleDomainValue(VarID varID) const
{
	vxy_assert(varID.isValid());
	return getSolver()->getDomain(varID).getValueForIndex(getMaximumPossibleValue(varID));
}

SolverVariableDatabase::SolverVariableDatabase(ConstraintSolver* inSolver)
	: IVariableDatabase()
	, m_solver(inSolver)
{
	// Dummy for invalid (0 index) var
	m_variableInfo.push_back({ValueSet(), -1});
	m_lastSolvedValues.push_back(0);
	m_initialValues.push_back({});
	m_variableNames.push_back({});
}

void SolverVariableDatabase::onInitialArcConsistency()
{
	m_initialValues.clear();
	m_initialValues.push_back({}); // dummy index

	for (int i = 1; i < m_variableInfo.size(); ++i)
	{
		m_initialValues.push_back(m_variableInfo[i].potentialValues);
		m_variableInfo[i].latestModification = -1;
	}
	m_assignmentStack.reset();
	m_isSolving = true;
}

void SolverVariableDatabase::onContradiction(VarID varID, ISolverConstraint* constraint, const ExplainerFunction& explainer)
{
	// This is a good spot to set a breakpoint if trying to determine why a variable was narrowed.
	vxy_assert(!m_lastContradictingVar.isValid());
	m_lastContradictingVar = varID;
}

WatcherHandle SolverVariableDatabase::addVariableWatch(VarID var, EVariableWatchType watchType, IVariableWatchSink* sink)
{
	return m_solver->addVariableWatch(var, watchType, sink);
}

WatcherHandle SolverVariableDatabase::addVariableValueWatch(VarID var, const ValueSet& values, IVariableWatchSink* sink)
{
	return m_solver->addVariableValueWatch(var, values, sink);
}

void SolverVariableDatabase::disableWatcherUntilBacktrack(WatcherHandle handle, VarID variable, IVariableWatchSink* sink)
{
	m_solver->disableWatcherUntilBacktrack(handle, variable, sink);
}

void SolverVariableDatabase::removeVariableWatch(VarID var, WatcherHandle handle, IVariableWatchSink* sink)
{
	vxy_assert(var.isValid());
	m_solver->removeVariableWatch(var, handle, sink);
}

VarID SolverVariableDatabase::addVariableImpl(const wstring& name, int domainSize, const vector<int>& potentialValues)
{
	vxy_assert(!m_isSolving);

	ValueSet values(domainSize, potentialValues.empty() ? true : false);
	for (int val : potentialValues)
	{
		values[val] = true;
	}

	VarID varID(m_variableInfo.size());
	m_variableInfo.push_back({values, AssignmentStack::TIMESTAMP_INITIAL});
	m_lastSolvedValues.push_back(0);
	m_initialValues.push_back(values);
	m_variableNames.push_back(name);

	return varID;
}

void SolverVariableDatabase::setInitialValue(VarID variable, const ValueSet& values)
{
	vxy_assert(!m_isSolving);
	vxy_assert(variable.isValid());
	m_variableInfo[variable.raw()].potentialValues = values;
	m_initialValues[variable.raw()] = values;
}

ValueSet& SolverVariableDatabase::lockVariableImpl(VarID varID)
{
	vxy_assert(varID.isValid());
	vxy_assert(!m_lockedVar.isValid());
	m_lockedVar = varID;
	m_lockedValues = m_variableInfo[varID.raw()].potentialValues;
	return m_lockedValues;
}

SolverTimestamp SolverVariableDatabase::makeDecision(VarID variable, int value)
{
	vxy_assert(m_isSolving);

	bool b = constrainToValue(variable, value, nullptr);
	vxy_assert(b);

	return m_assignmentStack.getMostRecentTimestamp();
}

void SolverVariableDatabase::unlockVariableImpl(VarID varID, bool wasChanged, ISolverConstraint* constraint, ExplainerFunction explainer)
{
	vxy_assert(varID.isValid());
	vxy_assert(m_lockedVar == varID);
	m_lockedVar.reset();

	if (wasChanged)
	{
		// ensure we're not widening the domain
		vxy_sanity(m_lockedValues.including(m_variableInfo[varID.raw()].potentialValues) == m_variableInfo[varID.raw()].potentialValues);

		auto& info = m_variableInfo[varID.raw()];
		ValueSet prev = info.potentialValues;
		SolverTimestamp timestamp = m_assignmentStack.recordChange(varID, prev, info.latestModification, constraint, move(explainer));
		vxy_assert(prev.size() == m_lockedValues.size());

		if (auto learned = constraint ? constraint->asClauseConstraint() : nullptr; learned && learned->isLearned())
		{
			// Ensure this constraint is not removed while it's part of the solution
			learned->lock();
		}

		for (auto& heuristic : m_solver->getDecisionHeuristics())
		{
			heuristic->onVariableAssignment(varID, info.potentialValues, m_lockedValues);
		}

		info.latestModification = timestamp;
		info.potentialValues = move(m_lockedValues);

		m_solver->notifyVariableModification(varID, constraint);
	}
}

void SolverVariableDatabase::queueConstraintPropagation(ISolverConstraint* constraint)
{
	m_solver->queueConstraintPropagation(constraint);
}

bool SolverVariableDatabase::getLastSolvedValue(VarID varID, int& outValue) const
{
	if (m_lastSolvedValues[varID.raw()] != 0)
	{
		outValue = m_lastSolvedValues[varID.raw()] - 1;
		return true;
	}

	return false;
}

void SolverVariableDatabase::clearLastSolvedValues()
{
	for (int i = 0; i < m_lastSolvedValues.size(); ++i)
	{
		m_lastSolvedValues[i] = 0;
	}
}

const ValueSet& SolverVariableDatabase::getValueBefore(VarID variable, SolverTimestamp timestamp, SolverTimestamp* outTimestamp) const
{
	if (timestamp <= 0)
	{
		return m_initialValues[variable.raw()];
	}

	int temp = 0;
	if (outTimestamp == nullptr)
	{
		outTimestamp = &temp;
	}

	const VariableInfo& varInfo = m_variableInfo[variable.raw()];
	const ValueSet* found = &varInfo.potentialValues;

	SolverTimestamp t = varInfo.latestModification;
	*outTimestamp = t;

	auto& stack = m_assignmentStack.getStack();
	while (t >= timestamp)
	{
		vxy_assert(stack[t].variable == variable);
		found = &stack[t].previousValue;
		t = stack[t].previousVariableAssignment;

		*outTimestamp = t;
	}
	return *found;
}

const ValueSet& SolverVariableDatabase::getValueAfter(VarID varID, SolverTimestamp timestamp) const
{
	const VariableInfo& info = m_variableInfo[varID.raw()];
	const ValueSet* after = &info.potentialValues;

	SolverTimestamp t = info.latestModification;
	auto& stack = m_assignmentStack.getStack();
	while (t >= 0 && t > timestamp)
	{
		vxy_assert(stack[t].variable == varID);
		after = &stack[t].previousValue;
		t = stack[t].previousVariableAssignment;
	}
	return *after;
}

SolverTimestamp SolverVariableDatabase::getModificationTimePriorTo(VarID variable, SolverTimestamp timestamp) const
{
	const VariableInfo& info = m_variableInfo[variable.raw()];
	auto& stack = m_assignmentStack.getStack();

	if (timestamp < 0)
	{
		return timestamp;
	}

	SolverTimestamp t = info.latestModification;
	while (t >= timestamp)
	{
		vxy_assert(stack[t].variable == variable);
		t = stack[t].previousVariableAssignment;
	}

	return t;
}

void SolverVariableDatabase::backtrack(SolverTimestamp timestamp)
{
	vxy_assert(m_isSolving);
	m_assignmentStack.backtrackToTime(timestamp, [&](const AssignmentStack::Modification& mod)
	{
		VariableInfo& varInfo = m_variableInfo[mod.variable.raw()];

		int solvedValue;
		if (varInfo.potentialValues.isSingleton(solvedValue))
		{
			m_lastSolvedValues[mod.variable.raw()] = solvedValue + 1;
		}

		for (auto& heuristic : m_solver->getDecisionHeuristics())
		{
			heuristic->onVariableUnassignment(mod.variable, varInfo.potentialValues, mod.previousValue);
		}
		varInfo.latestModification = mod.previousVariableAssignment;
		varInfo.potentialValues = mod.previousValue;

		// Unlock the learned clause that was locked when this entry was put on the stack
		if (auto cons = mod.constraint ? mod.constraint->asClauseConstraint() : nullptr; cons && cons->isLearned())
		{
			cons->unlock();
		}
		resetVariableState(mod.variable);
	});
	m_lastContradictingVar = VarID::INVALID;
}

SolverDecisionLevel SolverVariableDatabase::getDecisionLevelForTimestamp(SolverTimestamp timestamp) const
{
	return m_solver->getDecisionLevelForTimestamp(timestamp);
}

SolverDecisionLevel SolverVariableDatabase::getDecisionLevel() const
{
	return m_solver->getCurrentDecisionLevel();
}

SolverDecisionLevel SolverVariableDatabase::getDecisionLevelForVariable(VarID var) const
{
	vxy_assert(var.isValid());
	return m_solver->getVariableToDecisionLevelMap()[var.raw()];
}

bool SolverVariableDatabase::hasFinishedInitialArcConsistency() const
{
	return m_solver->hasFinishedInitialArcConsistency();
}
