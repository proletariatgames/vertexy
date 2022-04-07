// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once

#include "ConstraintTypes.h"
#include "SegmentedVariablePropagator.h"
#include "variable/IVariableWatchSink.h"

namespace Vertexy
{

class IVariableWatchSink;

// Implementation of IVariablePropagator for handling bitfields > 1 DWORD in size
class GenericVariablePropagator final : public SegmentedVariablePropagator
{
public:
	GenericVariablePropagator(int domainSize);

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

	// Whether there are any watchers for a specific flag
	inline bool hasWatchersForFlag(EVariableWatchType flag) const { return m_flagCounts[int(flag)] > 0; }

	// Trigger watchers based on the input flags
	virtual bool trigger(VarID variable, const ValueSet& prevValue, const ValueSet& currentValue, IVariableDatabase* db, IVariableWatchSink** currentSink, SolverTimestamp& triggeredTime) override;

protected:
	struct ValueSinkKey
	{
		ValueSet values;
		int cachedMin;
		int cachedMax;

		inline bool operator==(const ValueSinkKey& rhs) const
		{
			return values == rhs.values;
		}
	};

	void processPendingDeletes();
	bool triggerSinks(int segment, VarID variable, const ValueSet& prevValue, IVariableDatabase* db, IVariableWatchSink** currentSink, SolverTimestamp& triggeredTime, int handlePoolIdx = -1);
	bool removeWatcherFromList(int segment, WatcherHandle handle);

	inline WatcherHandle createWatcherHandle(int type)
	{
		// Top three bits for type mask, remainder for ID
		vxy_assert(type < 7);
		const uint32_t typeMask = static_cast<uint32_t>(type) << 29;
		const uint32_t id = m_nextHandle[type]++;
		vxy_sanity(id < 0x1FFFFFFF);
		vxy_sanity(getTypeFromHandle(typeMask|id) == type);
		return typeMask | id;
	}

	inline uint32_t getIndexFromHandle(WatcherHandle handle) const
	{
		return (handle & 0x1FFFFFFF);
	}

	inline uint32_t getTypeFromHandle(WatcherHandle handle) const
	{
		const uint32_t type = handle >> 29;
		vxy_assert(type < 7);
		return type;
	}

	inline int getFirstSingleOnSegment() const
	{
		return int(EVariableWatchType::NUM_WATCH_TYPES);
	}

	inline int getEndSingleOnSegment() const
	{
		return getFirstSingleOnSegment() + m_segmentToOnKeys.size();
	}

	inline int getFirstSingleOffSegment() const
	{
		return int(EVariableWatchType::NUM_WATCH_TYPES) + m_segmentToOnKeys.size();
	}

	inline int getEndSingleOffSegment() const
	{
		return getFirstSingleOffSegment() + m_segmentToOffKeys.size();
	}

	inline int getFirstValueSegment() const
	{
		return int(EVariableWatchType::NUM_WATCH_TYPES) + m_segmentToOnKeys.size() + m_segmentToOffKeys.size();
	}

	inline int getEndValueSegment() const
	{
		return m_segments.size();
	}

	// For each watch flag, the count of watchers of that type
	int m_flagCounts[EVariableWatchType::NUM_WATCH_TYPES];

	// Whether we're currently iterating
	bool m_iterating = false;

	// Whether we have sinks pending deletion
	bool m_anyPendingDelete = false;

	// How many watches are registered total
	int m_numWatches = 0;

	// Handle to assign to next watcher added
	WatcherHandle m_nextHandle[7] = {0, 0, 0, 0, 0, 0, 0};

	static constexpr int HANDLE_TYPE_SINGLE_ON = 4;
	static constexpr int HANDLE_TYPE_SINGLE_OFF = 5;
	static constexpr int HANDLE_TYPE_VALUE = 6;

	static constexpr int POOL_SINGLE_ON = 0;
	static constexpr int POOL_SINGLE_OFF = 1;
	static constexpr int POOL_VALUE = 2;

	int m_domainSize;

	// Handle index -> SegmentToValueKey index that is being watched
	vector<int> m_handleToWatchedValuesKey;
	// Handle index -> single-bit=on/single-bit-off value that is being watched
	vector<int> m_handleToSingleValue[2];
	// Free handles by type (single-bit-on, single-bit-off, value)
	vector<WatcherHandle> m_freeHandlesByType[3];
	// For single-bit-on segments, the bit being watched per segment.
	vector<int> m_segmentToOnKeys;
	// For single-bit-off segments, the bit being watched per segment.
	vector<int> m_segmentToOffKeys;
	// For value-watch segments, the value being watched per segment.
	vector<ValueSinkKey> m_segmentToValueKey;
};

} // namespace Vertexy