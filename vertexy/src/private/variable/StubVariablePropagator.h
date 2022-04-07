// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once

#include "ConstraintTypes.h"
#include "variable/IVariablePropagator.h"
#include "variable/IVariableWatchSink.h"

namespace Vertexy
{

class IVariableWatchSink;

// Stub implementation of IVariablePropagator for handling variables with only 1 potential value
class StubVariablePropagator final : public IVariablePropagator
{
public:
	StubVariablePropagator()
	{
	}

	// Returns the total number of watches
	virtual int getNumWatches() const override { return m_numWatches; }

	// Adds a watcher to the list
	virtual WatcherHandle addWatcher(IVariableWatchSink* sink, EVariableWatchType watchType) override
	{
		++m_numWatches;
		return INVALID_WATCHER_HANDLE;
	}

	// Adds a watcher to the list for when value(s) get removed
	virtual WatcherHandle addValueWatcher(IVariableWatchSink* sink, const ValueSet& watchValues) override
	{
		++m_numWatches;
		return INVALID_WATCHER_HANDLE;
	}

	virtual bool setWatcherEnabled(WatcherHandle handle, IVariableWatchSink* sink, bool bEnabled) override
	{
		return false;
	}

	// Removes a watcher from the list
	virtual void removeWatcher(WatcherHandle handle, IVariableWatchSink* sink) override
	{
		--m_numWatches;
	}

	// Trigger watchers based on the input flags
	virtual bool trigger(VarID variable, const ValueSet& prevValue, const ValueSet& currentValue, IVariableDatabase* db, IVariableWatchSink** currentSink, SolverTimestamp& timestamp) override
	{
		return true;
	}

protected:
	// How many watches are registered total
	int m_numWatches = 0;
};

} // namespace Vertexy