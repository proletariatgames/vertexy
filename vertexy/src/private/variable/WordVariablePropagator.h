// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once

#include "ConstraintTypes.h"
#include "SegmentedVariablePropagator.h"
#include "variable/IVariableWatchSink.h"

namespace Vertexy
{

class IVariableWatchSink;

// Implementation of IVariablePropagator for handling bitfields that are less than one machine word
template <typename WORD_TYPE>
class TWordVariablePropagator final : public SegmentedVariablePropagator
{
public:
	TWordVariablePropagator(int domainSize);

	// Returns the total number of watches
	virtual int getNumWatches() const override { return m_numWatches; }

	// Adds a watcher to the list
	virtual WatcherHandle addWatcher(IVariableWatchSink* sink, EVariableWatchType watchType) override;

	// Adds a watcher to the list for when value(s) get removed
	virtual WatcherHandle addValueWatcher(IVariableWatchSink* sink, const ValueSet& watchValues) override;

	// Removes a watcher from the list
	virtual void removeWatcher(WatcherHandle handle, IVariableWatchSink* sink) override;

	// Disables/Reeanbles a watcher. Disabled watchers will persist in memory but not trigger.
	virtual bool setWatcherEnabled(WatcherHandle handle, IVariableWatchSink* sink, bool enabled) override;

	// Whether there are any watchers for a specific flag
	inline bool hasWatchersForFlag(EVariableWatchType flag) const { return m_flagCounts[int(flag)] > 0; }

	// Trigger watchers based on the input flags
	virtual bool trigger(VarID variable, const ValueSet& prevValue, const ValueSet& currentValue, IVariableDatabase* db, IVariableWatchSink** currentSink, SolverTimestamp& triggeredTime) override;

protected:
	void processPendingDeletes();

	inline WatcherHandle createWatcherHandle(EVariableWatchType type)
	{
		// Top three bits for type mask, remainder for ID
		const uint32_t typeMask = static_cast<uint32_t>(type) << 28;
		const uint32_t id = m_nextHandle++;
		vxy_sanity(id < 0x1FFFFFFF);
		return typeMask | id;
	}

	inline uint32_t getTypeFromHandle(WatcherHandle handle)
	{
		const uint32_t type = handle >> 28;
		vxy_assert(type <= static_cast<uint32_t>(EVariableWatchType::NUM_WATCH_TYPES));
		return type;
	}

	bool triggerSinks(int segment, VarID variable, const ValueSet& prevValue, IVariableDatabase* db, IVariableWatchSink** currentConstraint, SolverTimestamp& triggeredTs);

	// For each watch flag, the count of watchers of that type
	int m_flagCounts[EVariableWatchType::NUM_WATCH_TYPES];

	// Whether we're currently iterating
	bool m_iterating = false;

	// Whether we have sinks pending deletion
	bool m_anyPendingDelete = false;

	// How many watches are registered total
	int m_numWatches = 0;

	// Handle to assign to next watcher added
	WatcherHandle m_nextHandle = 0;

	// Size of the actual domain (may be less than word length)
	int m_domainSize;

	// Masks out any unused bits in the word
	WORD_TYPE m_mask;

	// For each value sink segment, the values that should trigger that sink if removed
	vector<WORD_TYPE> m_valueSinkKeys;
};

// NOTE: these are the only two instantiated versions of this template!
using WordVariablePropagator = TWordVariablePropagator<uint32_t>;
using DwordVariablePropagator = TWordVariablePropagator<uint64_t>;

} // namespace Vertexy