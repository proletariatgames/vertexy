// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once

#include "ConstraintTypes.h"
#include "IVariableWatchSink.h"

namespace Vertexy
{
class IVariableWatchSink;

// Interface for variable watchers, which notifies constraints when a variable loses potential values.
class IVariablePropagator
{
public:
	virtual ~IVariablePropagator()
	{
	}

	// Trigger watchers based on the input flags
	virtual bool trigger(VarID variable, const ValueSet& prevValue, const ValueSet& currentValue, IVariableDatabase* db, IVariableWatchSink** currentSink, SolverTimestamp& lastTriggeredTS) = 0;

	// Returns the total number of watches
	virtual int getNumWatches() const = 0;

	// Adds a watcher to the list
	virtual WatcherHandle addWatcher(IVariableWatchSink* sink, EVariableWatchType watchType) = 0;

	// Adds a watcher to the list for when value(s) get removed
	virtual WatcherHandle addValueWatcher(IVariableWatchSink* sink, const ValueSet& watchValues) = 0;

	// Disables/Re-enables a watcher. Disabled watchers will persist in memory but not trigger.
	// @return true if state changed.
	virtual bool setWatcherEnabled(WatcherHandle handle, IVariableWatchSink* sink, bool enabled) = 0;

	// Removes a watcher from the list
	virtual void removeWatcher(WatcherHandle handle, IVariableWatchSink* sink) = 0;
};

} // namespace Vertexy