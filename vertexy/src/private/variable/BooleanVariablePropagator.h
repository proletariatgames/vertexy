// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once

#include "ConstraintTypes.h"
#include "SegmentedVariablePropagator.h"
#include "variable/IVariablePropagator.h"
#include "variable/IVariableWatchSink.h"

namespace Vertexy
{

class IVariableWatchSink;

// Implementation of IVariablePropagator for handling boolean variables
class BooleanVariablePropagator final : public SegmentedVariablePropagator
{
public:
	BooleanVariablePropagator();

	// Returns the total number of watches
	virtual int getNumWatches() const override { return m_numWatches; }

	// Adds a watcher to the list
	virtual WatcherHandle addWatcher(IVariableWatchSink* sink, EVariableWatchType watchType) override;

	// Adds a watcher to the list for when value(s) get removed
	virtual WatcherHandle addValueWatcher(IVariableWatchSink* sink, const ValueSet& watchValues) override;

	// Disables/Reeanbles a watcher. Disabled watchers will persist in memory but not trigger.
	virtual bool setWatcherEnabled(WatcherHandle handle, IVariableWatchSink* sink, bool enabled) override;

	// Removes a watcher from the list
	virtual void removeWatcher(WatcherHandle handle, IVariableWatchSink* sink) override;

	// Trigger watchers based on the input flags
	virtual bool trigger(VarID variable, const ValueSet& prevValue, const ValueSet& currentValue, IVariableDatabase* db, IVariableWatchSink** currentSink, SolverTimestamp& triggeredTime) override;

protected:
	void processPendingDeletes();

	inline WatcherHandle createWatcherHandle(int segment)
	{
		// Top two bits for segment, remainder for ID
		vxy_assert(segment >= 0 && segment < 3);
		const int typeMask = static_cast<uint32_t>(segment) << 30;
		const int id = m_nextHandle++;
		vxy_sanity(id < 0x3FFFFFFF);
		return typeMask | id;
	}

	inline uint32_t getSegmentFromHandle(WatcherHandle handle)
	{
		const uint32_t type = handle >> 30;
		vxy_assert(type < 3);
		return type;
	}

	// Whether we're currently iterating
	bool m_iterating = false;

	// Whether we have sinks pending deletion
	bool m_anyPendingDelete = false;

	// How many watches are registered total
	int m_numWatches = 0;

	// Handle to assign to next watcher added
	WatcherHandle m_nextHandle = 0;
};

} // namespace Vertexy