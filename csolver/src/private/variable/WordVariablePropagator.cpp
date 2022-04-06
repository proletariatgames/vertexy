// Copyright Proletariat, Inc. All Rights Reserved.
#include "WordVariablePropagator.h"
#include "constraints/ISolverConstraint.h"
#include "variable/IVariableDatabase.h"
#include "util/BitUtils.h"

//#define sanityCheck(s)
#define sanityCheck(s) cs_sanity(s)

using namespace csolver;

template TWordVariablePropagator<uint32_t>;
template TWordVariablePropagator<uint64_t>;

template <typename WORD_TYPE>
TWordVariablePropagator<WORD_TYPE>::TWordVariablePropagator(int domainSize)
	: m_domainSize(domainSize)
{
	m_segments.reserve(int(EVariableWatchType::NUM_WATCH_TYPES));
	for (int i = 0; i < int(EVariableWatchType::NUM_WATCH_TYPES); ++i)
	{
		m_flagCounts[i] = 0;
		m_segments.push_back({0, 0});
	}

	m_mask = BitUtils::computeMask<WORD_TYPE>(domainSize);
}

template <typename WORD_TYPE>
bool TWordVariablePropagator<WORD_TYPE>::trigger(VarID variable, const ValueSet& prevValue, const ValueSet& currentValue, IVariableDatabase* db, IVariableWatchSink** currentSink, SolverTimestamp& triggeredTime)
{
	cs_assert(!m_iterating);
	cs_assert(!m_anyPendingDelete);

	if (m_numWatches == 0)
	{
		return true;
	}

	cs_sanity(db->getDomainSize(variable) < sizeof(WORD_TYPE)*8);

	// Ensure that 64-bit layout is same as two 32 bits in a row.
	// If false, then we cannot use TWordVariablePropagator<uint64_t>
	{
		static constexpr uint32_t Words[2] = {0x1U, 0x1U};
		static constexpr uint64_t DWord = (0x1ULL << 32) | 0x1ULL;
		static_assert(Words[0] == (DWord & 0x00000000FFFFFFFFULL), "Machine word layout not compatible with TWordVariablePropagator<unsigned long long>");
		static_assert(Words[1] == (DWord & 0xFFFFFFFF00000000ULL) >> 32, "Machine word layout not compatible with TWordVariablePropagator<unsigned long long>");
	}

	WORD_TYPE currentWord = (*reinterpret_cast<const WORD_TYPE*>(currentValue.data())) & m_mask;
	WORD_TYPE prevWord = (*reinterpret_cast<const WORD_TYPE*>(prevValue.data())) & m_mask;

	int flags = 1 << int(EVariableWatchType::WatchModification);
	if (hasWatchersForFlag(EVariableWatchType::WatchSolved))
	{
		// mask out the most significant bit. If result is zero, then only one bits is set.
		if ((currentWord & (currentWord - 1)) == 0)
		{
			sanityCheck(currentValue.isSingleton());
			flags |= 1 << int(EVariableWatchType::WatchSolved);
		}
		else
		{
			sanityCheck(!currentValue.isSingleton());
		}
	}
	if (hasWatchersForFlag(EVariableWatchType::WatchLowerBoundChange))
	{
		WORD_TYPE prevZeros = BitUtils::countTrailingZeros(prevWord);
		WORD_TYPE curZeros = BitUtils::countTrailingZeros(currentWord);
		sanityCheck(prevZeros <= curZeros);
		if (curZeros > prevZeros)
		{
			flags |= 1 << int(EVariableWatchType::WatchLowerBoundChange);
			sanityCheck(currentValue.indexOf(true) > prevValue.indexOf(true));
		}
		else
		{
			sanityCheck(currentValue.indexOf(true) == prevValue.indexOf(true));
		}
	}
	if (hasWatchersForFlag(EVariableWatchType::WatchUpperBoundChange) && currentValue.lastIndexOf(true) < prevValue.lastIndexOf(true))
	{
		WORD_TYPE prevZeros = BitUtils::countLeadingZeros(prevWord);
		WORD_TYPE curZeros = BitUtils::countLeadingZeros(currentWord);
		sanityCheck(prevZeros <= curZeros);
		if (curZeros > prevZeros)
		{
			flags |= 1 << int(EVariableWatchType::WatchUpperBoundChange);
			sanityCheck(currentValue.lastIndexOf(true) < prevValue.lastIndexOf(true));
		}
		else
		{
			sanityCheck(currentValue.lastIndexOf(true) == prevValue.lastIndexOf(true));
		}
	}

	bool result = true;
	ValueGuard<bool> guard(m_iterating, true);

	int segment;
	for (segment = 0; segment < int(EVariableWatchType::NUM_WATCH_TYPES); ++segment)
	{
		if ((flags & (1 << segment)) == 0)
		{
			continue;
		}

		if (!triggerSinks(segment, variable, prevValue, db, currentSink, triggeredTime))
		{
			result = false;
			goto Failure;
		}
	}

	//
	// Check if any value sinks need to be visited, and trigger any that do.
	//
	for (; segment < m_segments.size(); ++segment)
	{
		const WORD_TYPE key = m_valueSinkKeys[segment - int(EVariableWatchType::NUM_WATCH_TYPES)];
		if ((currentWord & key) != 0)
		{
			continue;
		}

		if (!triggerSinks(segment, variable, prevValue, db, currentSink, triggeredTime))
		{
			result = false;
			goto Failure;
		}
	}

Failure:
	if (m_anyPendingDelete)
	{
		processPendingDeletes();
	}

	return result;
}

template <typename WORD_TYPE>
bool TWordVariablePropagator<WORD_TYPE>::triggerSinks(int segment, VarID variable, const ValueSet& prevValue, IVariableDatabase* db, IVariableWatchSink** currentSink, SolverTimestamp& triggeredTs)
{
	bool result = true;
	bool remove = false;
	const int segStart = m_segments[segment].start;
	const int segEnd = m_segments[segment].end;
	for (int i = segEnd - 1; i >= segStart; --i)
	{
		if (IVariableWatchSink* sink = m_entries[i])
		{
			*currentSink = sink;
			triggeredTs = db->getTimestamp();

			result = sink->onVariableNarrowed(db, variable, prevValue, remove);

			if (remove)
			{
				removeSinkAt(segment, i);
				remove = false;
			}

			if (!result)
			{
				break;
			}
		}
	}
	return result;
}

template <typename WORD_TYPE>
WatcherHandle TWordVariablePropagator<WORD_TYPE>::addWatcher(IVariableWatchSink* sink, EVariableWatchType watchType)
{
	++m_numWatches;
	WatcherHandle handle = createWatcherHandle(watchType);
	insertSink(int(watchType), handle, sink);
	m_flagCounts[int(watchType)]++;
	return handle;
}

template <typename WORD_TYPE>
WatcherHandle TWordVariablePropagator<WORD_TYPE>::addValueWatcher(IVariableWatchSink* sink, const ValueSet& watchValues)
{
	cs_sanity(!watchValues.isZero());
	const int segmentOffs = int(EVariableWatchType::NUM_WATCH_TYPES);

	WORD_TYPE key = (*reinterpret_cast<const WORD_TYPE*>(watchValues.data())) & m_mask;

	int segment = indexOf(m_valueSinkKeys.begin(), m_valueSinkKeys.end(), key);
	if (segment < 0)
	{
		segment = m_segments.size() - segmentOffs;
		m_segments.emplace_back(m_entries.size(), m_entries.size());
		m_valueSinkKeys.push_back(key);
	}

	WatcherHandle handle = createWatcherHandle(EVariableWatchType::NUM_WATCH_TYPES);
	++m_numWatches;

	insertSink(segment + segmentOffs, handle, sink);

	return handle;
}

template <typename WORD_TYPE>
bool TWordVariablePropagator<WORD_TYPE>::setWatcherEnabled(WatcherHandle handle, IVariableWatchSink* sink, bool enabled)
{
	auto scanAndMark = [&](int start, int end, bool& changed)
	{
		changed = false;
		for (int i = start; i < end; ++i)
		{
			if (m_handles[i] == handle)
			{
				cs_assert(!m_markedForRemoval[i]);
				if (enabled)
				{
					if (m_entries[i] == nullptr)
					{
						m_entries[i] = sink;
						changed = true;
					}
				}
				else
				{
					if (m_entries[i] != nullptr)
					{
						cs_assert(m_entries[i] == sink);
						m_entries[i] = nullptr;
						changed = true;
					}
				}
				return true;
			}
		}
		return false;
	};

	uint32_t watchType = getTypeFromHandle(handle);
	if (watchType < static_cast<uint32_t>(EVariableWatchType::NUM_WATCH_TYPES))
	{
		bool changed;
		cs_verify(scanAndMark(m_segments[watchType].start, m_segments[watchType].end, changed));
		return changed;
	}
	else
	{
		for (int segment = watchType; segment < m_segments.size(); ++segment)
		{
			bool changed;
			if (scanAndMark(m_segments[segment].start, m_segments[segment].end, changed))
			{
				return changed;
			}
		}
	}

	cs_fail();
	return false;
}

template <typename WORD_TYPE>
void TWordVariablePropagator<WORD_TYPE>::removeWatcher(WatcherHandle handle, IVariableWatchSink* sink)
{
	if (handle == INVALID_WATCHER_HANDLE)
	{
		return;
	}

	uint32_t watchType = getTypeFromHandle(handle);
	if (watchType < static_cast<uint32_t>(EVariableWatchType::NUM_WATCH_TYPES))
	{
		const SinkSegment& seg = m_segments[watchType];
		for (int i = seg.start; i < seg.end; ++i)
		{
			if (m_handles[i] == handle)
			{
				if (m_iterating)
				{
					m_entries[i] = nullptr;
					m_markedForRemoval[i] = true;
					m_anyPendingDelete = true;
				}
				else
				{
					removeSinkAt(watchType, i);

					--m_numWatches;
					m_flagCounts[watchType]--;
				}
				return;
			}
		}
	}
	else
	{
		constexpr int startSegment = int(EVariableWatchType::NUM_WATCH_TYPES);
		for (int segment = startSegment; segment < m_segments.size(); ++segment)
		{
			const SinkSegment& seg = m_segments[segment];
			for (int i = seg.start; i < seg.end; ++i)
			{
				if (m_handles[i] == handle)
				{
					if (m_iterating)
					{
						m_entries[i] = nullptr;
						m_markedForRemoval[i] = true;
						m_anyPendingDelete = true;
					}
					else
					{
						removeSinkAt(segment, i);
						--m_numWatches;
					}

					return;
				}
			}
		}
	}
}

template <typename WORD_TYPE>
void TWordVariablePropagator<WORD_TYPE>::processPendingDeletes()
{
	cs_assert(m_anyPendingDelete);
	for (int seg = 0; seg < m_segments.size(); ++seg)
	{
		for (int i = m_segments[seg].end - 1; i >= m_segments[seg].start; --i)
		{
			if (m_markedForRemoval[i])
			{
				removeSinkAt(seg, i);
				--m_numWatches;
				if (seg < int(EVariableWatchType::NUM_WATCH_TYPES))
				{
					m_flagCounts[seg]--;
				}
			}
		}
	}

	m_anyPendingDelete = false;
}
