// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once

#include <EASTL/hash_map.h>
#include <EASTL/functional.h>

#include "ConstraintTypes.h"
#include "variable/IVariableDatabase.h"
#include "variable/IVariableWatchSink.h"

namespace Vertexy
{
class CommittableVariableDatabase;

// Interface for CommittableVariableDatabase callbacks
class ICommittableVariableDatabaseOwner
{
public:
	virtual ~ICommittableVariableDatabaseOwner() {}

	virtual void committableDatabaseQueueRequest(const CommittableVariableDatabase& db, IConstraint* cons) = 0;
	virtual WatcherHandle committableDatabaseAddWatchRequest(const CommittableVariableDatabase& db, VarID varID, EVariableWatchType watchType, IVariableWatchSink* sink) = 0;
	virtual WatcherHandle committableDatabaseAddValueWatchRequest(const CommittableVariableDatabase& db, VarID varID, const ValueSet& values, IVariableWatchSink* sink) = 0;
	virtual void committableDatabaseDisableWatchRequest(const CommittableVariableDatabase& db, WatcherHandle handle, VarID variable, IVariableWatchSink* sink) = 0;
	virtual void committableDatabaseRemoveWatchRequest(const CommittableVariableDatabase& db, VarID varID, WatcherHandle handle, IVariableWatchSink* sink) = 0;
	virtual ExplainerFunction committableDatabaseWrapExplanation(const CommittableVariableDatabase& db, const ExplainerFunction& innerExpl) = 0;
	virtual void committableDatabaseContradictionFound(const CommittableVariableDatabase& db, VarID varID, IConstraint* source, const ExplainerFunction& explainer) = 0;
	virtual void committableDatabaseConstraintSatisfied(const CommittableVariableDatabase& db, IConstraint* constraint) = 0;
};

// Database that stores modifications to variables, which can later be committed to the parent database by calling Commit().
// This is used for disjunctions where we need to test/propagate constraints to check satisfiability, but don't want to
// actually have them propagate to the database until the constraint disjunction is unit.
class CommittableVariableDatabase : public IVariableDatabase
{
public:
	CommittableVariableDatabase() = delete;
	CommittableVariableDatabase(IVariableDatabase* inParent, IConstraint* outerCons, ICommittableVariableDatabaseOwner* outerSink, int outerSinkID=-1)
		: m_parent(inParent)
		, m_outerCons(outerCons)
		, m_outerSink(outerSink)
		, m_outerSinkID(outerSinkID)
	{
		m_numVariables = m_parent->getNumVariables();
		#if CONSTRAINT_USE_CACHED_STATES
		m_states.resize(m_parent->getNumVariables() + 1, EVariableState::Unknown);
		#endif
	}

	bool commitPastAndFutureChanges();
	bool hasContradiction() const { return m_hasContradiction; }
	IVariableDatabase* getParent() const { return m_parent; }
	int getOuterSinkID() const { return m_outerSinkID; }

	virtual ValueSet& lockVariableImpl(VarID varID) override;
	virtual void unlockVariableImpl(VarID varID, bool wasChanged, IConstraint* constraint, ExplainerFunction explainer) override;
	virtual void onContradiction(VarID varID, IConstraint* constraint, const ExplainerFunction& explainer) override;
	virtual SolverDecisionLevel getDecisionLevel() const override { return m_parent->getDecisionLevel(); }
	virtual SolverTimestamp getTimestamp() const override { return m_parent->getTimestamp() + m_modifications.size(); }
	virtual const ValueSet& getPotentialValues(VarID varID) const override;
	virtual void queueConstraintPropagation(IConstraint* constraint) override;
	virtual WatcherHandle addVariableWatch(VarID varID, EVariableWatchType watchType, IVariableWatchSink* sink) override;
	virtual WatcherHandle addVariableValueWatch(VarID varID, const ValueSet& values, IVariableWatchSink* sink) override;
	virtual void disableWatcherUntilBacktrack(WatcherHandle handle, VarID variable, IVariableWatchSink* sink) override;
	virtual void removeVariableWatch(VarID varID, WatcherHandle handle, IVariableWatchSink* sink) override;
	virtual SolverDecisionLevel getDecisionLevelForVariable(VarID varID) const override;
	virtual SolverDecisionLevel getDecisionLevelForTimestamp(SolverTimestamp timestamp) const override;
	virtual SolverTimestamp getLastModificationTimestamp(VarID variable) const override;
	virtual const ValueSet& getInitialValues(VarID variable) const override;
	virtual const ValueSet& getValueBefore(VarID variable, SolverTimestamp timestamp, SolverTimestamp* outTimestamp = nullptr) const override;
	virtual const ValueSet& getValueAfter(VarID variable, SolverTimestamp timestamp) const override;
	virtual SolverTimestamp getModificationTimePriorTo(VarID variable, SolverTimestamp timestamp) const override;
	virtual const ConstraintSolver* getSolver() const override { return m_parent->getSolver(); }
	virtual void markConstraintFullySatisfied(IConstraint* constraint) override;

protected:
	IVariableDatabase* m_parent;
	IConstraint* m_outerCons;
	ICommittableVariableDatabaseOwner* m_outerSink;
	int m_outerSinkID = -1;

	struct VariableMod
	{
		VarID variable;
		ValueSet value;
		IConstraint* constraint;
		ExplainerFunction explainer;
	};


	vector<VariableMod> m_modifications;

	VarID m_lockedVar = VarID::INVALID;
	ValueSet m_lockedValues;
	bool m_hasContradiction = false;
	bool m_committed = false;
};

} // namespace Vertexy