// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once

#include "variable/IVariableDatabase.h"
#include "variable/SolverVariableDatabase.h"

namespace csolver
{

// Read-only variable database at a particular timestamp.
class HistoricalVariableDatabase : public IVariableDatabase
{
public:
	HistoricalVariableDatabase(const IVariableDatabase* db, SolverTimestamp timestamp)
		: m_db(db)
		, m_timestamp(timestamp)
	{
		m_numVariables = m_db->getNumVariables();
		#if CONSTRAINT_USE_CACHED_STATES
		m_states.resize(m_db->getNumVariables() + 1, EVariableState::Unknown);
		#endif
	}

	// Variable modifications not allowed
	virtual ValueSet& lockVariableImpl(VarID varID) override
	{
		cs_fail();
		static ValueSet none;
		return none;
	}

	// Variable modifications not allowed
	virtual void unlockVariableImpl(VarID varID, bool wasChanged, ISolverConstraint* constraint, ExplainerFunction explainerFn) override
	{
		cs_fail();
	}

	// Not allowed to modify watches
	virtual WatcherHandle addVariableWatch(VarID varID, EVariableWatchType watchType, IVariableWatchSink* sink) override
	{
		cs_fail();
		return INVALID_WATCHER_HANDLE;
	}

	virtual WatcherHandle addVariableValueWatch(VarID varID, const ValueSet& values, IVariableWatchSink* sink) override
	{
		cs_fail();
		return INVALID_WATCHER_HANDLE;
	}

	virtual void disableWatcherUntilBacktrack(WatcherHandle handle, VarID variable, IVariableWatchSink* sink) override
	{
		cs_fail();
	}

	// Not allowed to modify watches
	virtual void removeVariableWatch(VarID var, WatcherHandle handle, IVariableWatchSink* sink) override
	{
		cs_fail();
	}

	/** Override to return the current decision level of the solver. */
	virtual SolverDecisionLevel getDecisionLevel() const override;
	virtual SolverTimestamp getTimestamp() const override;
	virtual SolverDecisionLevel getDecisionLevelForVariable(VarID varID) const override;
	virtual SolverDecisionLevel getDecisionLevelForTimestamp(SolverTimestamp timestamp) const override;
	virtual SolverTimestamp getLastModificationTimestamp(VarID varID) const override;
	virtual const ValueSet& getInitialValues(VarID varID) const override;
	virtual const ValueSet& getValueBefore(VarID variable, SolverTimestamp timestamp, SolverTimestamp* outTimestamp = nullptr) const override;
	virtual const ValueSet& getValueAfter(VarID variable, SolverTimestamp timestamp) const override;
	virtual SolverTimestamp getModificationTimePriorTo(VarID variable, SolverTimestamp timestamp) const override;
	virtual const ConstraintSolver* getSolver() const override { return m_db->getSolver(); }

	/** Override to return the current (read-only) potential values for the given variable */
	virtual const ValueSet& getPotentialValues(VarID varID) const override;

	virtual void queueConstraintPropagation(ISolverConstraint* constraint) override
	{
		cs_fail();
	}

protected:
	const IVariableDatabase* m_db;
	SolverTimestamp m_timestamp;
};

} //namespace csolver