// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once

#include "IVariableDatabase.h"
#include "AssignmentStack.h"

namespace csolver
{
class ConstraintSolver;

/** Implementation of the solver's main variable database */
class SolverVariableDatabase final : public IVariableDatabase
{
public:
	explicit SolverVariableDatabase(ConstraintSolver* inSolver);

	virtual const ValueSet& getPotentialValues(VarID varID) const override
	{
		cs_assert(varID.isValid());
		return m_variableInfo[varID.raw()].potentialValues;
	}

	inline const AssignmentStack& getAssignmentStack() const { return m_assignmentStack; }
	inline AssignmentStack& getAssignmentStack() { return m_assignmentStack; }

	SolverTimestamp makeDecision(VarID variable, int value);

	virtual bool hasFinishedInitialArcConsistency() const override;
	virtual SolverDecisionLevel getDecisionLevel() const override;
	virtual SolverTimestamp getTimestamp() const override { return m_assignmentStack.getMostRecentTimestamp(); }
	virtual void onContradiction(VarID varID, ISolverConstraint* constraint, const ExplainerFunction& explainer) override;
	virtual void queueConstraintPropagation(ISolverConstraint* constraint) override;
	virtual WatcherHandle addVariableWatch(VarID var, EVariableWatchType watchType, IVariableWatchSink* sink) override;
	virtual WatcherHandle addVariableValueWatch(VarID var, const ValueSet& values, IVariableWatchSink* sink) override;
	virtual void disableWatcherUntilBacktrack(WatcherHandle handle, VarID variable, IVariableWatchSink* sink) override;
	virtual void removeVariableWatch(VarID var, WatcherHandle handle, IVariableWatchSink* sink) override;
	virtual SolverDecisionLevel getDecisionLevelForVariable(VarID var) const override;
	virtual SolverDecisionLevel getDecisionLevelForTimestamp(SolverTimestamp timestamp) const override;
	virtual const ValueSet& getValueBefore(VarID variable, SolverTimestamp timestamp, SolverTimestamp* outTimestamp = nullptr) const override;
	virtual const ValueSet& getValueAfter(VarID variable, SolverTimestamp timestamp) const override;
	virtual SolverTimestamp getModificationTimePriorTo(VarID variable, SolverTimestamp timestamp) const override;
	virtual const ConstraintSolver* getSolver() const override { return m_solver; }

	virtual SolverTimestamp getLastModificationTimestamp(VarID variable) const override
	{
		cs_assert(variable.isValid());
		return m_variableInfo[variable.raw()].latestModification;
	}

	virtual const ValueSet& getInitialValues(VarID variable) const override
	{
		cs_assert(variable.isValid());
		return m_initialValues[variable.raw()];
	}

	void setInitialValue(VarID variable, const ValueSet& values);

	VarID getLastContradictingVariable() const { return m_lastContradictingVar; }

	void onInitialArcConsistency();
	void backtrack(SolverTimestamp timestamp);

	/** Retrieve last value this variable was assigned to (which may have been un-done due to backtracking)
	 *  Returns false if this variable has not yet ever been solved.
	 */
	bool getLastSolvedValue(VarID varID, int& outValue) const;
	/** Clears all history of last solved values for all variables */
	void clearLastSolvedValues();

	const wstring& getVariableName(VarID varID) const
	{
		cs_assert(varID.isValid());
		return m_variableNames[varID.raw()];
	}

protected:
	virtual VarID addVariableImpl(const wstring& name, int domainSize, const vector<int>& potentialValues) override;
	virtual ValueSet& lockVariableImpl(VarID varID) override;
	virtual void unlockVariableImpl(VarID varID, bool wasChanged, ISolverConstraint* constraint, ExplainerFunction explainer) override;

	VarID m_lockedVar;
	ValueSet m_lockedValues;

	struct VariableInfo
	{
		// Current set of values remaining for this variable
		ValueSet potentialValues;
		// Last time this variable is modified: index into the assignment stack
		SolverTimestamp latestModification;
	};

	// Stores current (dis)assignments of each variable, and timestamp of last time variable was modified.
	vector<VariableInfo> m_variableInfo;

	// If non-zero, the value (+1) that this variable was last assigned to
	vector<int> m_lastSolvedValues;

	// History of all variable (dis)assignments
	AssignmentStack m_assignmentStack;

	// Initial values for each variable once initial arc consistency is established.
	vector<ValueSet> m_initialValues;

	// The solver that owns us
	ConstraintSolver* m_solver;

	// Stored separately for cache efficiency
	vector<wstring> m_variableNames;

	VarID m_lastContradictingVar;

	// Whether arc consistency has been established yet
	bool m_isSolving = false;
};

} // namespace csolver