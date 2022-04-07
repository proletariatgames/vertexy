// Copyright Proletariat, Inc. All Rights Reserved.
#include "GenericVariablePropagator.h"
#include "constraints/ISolverConstraint.h"
#include "variable/IVariableDatabase.h"

//#define sanityCheck(s)
#define sanityCheck(s) vxy_sanity(s)

using namespace Vertexy;

GenericVariablePropagator::GenericVariablePropagator(int domainSize)
	: m_domainSize(domainSize)
{
	m_segments.reserve(getFirstValueSegment());
	for (int i = 0; i < int(EVariableWatchType::NUM_WATCH_TYPES); ++i)
	{
		m_flagCounts[i] = 0;
		m_segments.push_back({0, 0});
	}
}

bool GenericVariablePropagator::trigger(VarID variable, const ValueSet& prevValue, const ValueSet& currentValue, IVariableDatabase* db, IVariableWatchSink** currentSink, SolverTimestamp& triggeredTime)
{
	vxy_assert(!m_iterating);
	vxy_assert(!m_anyPendingDelete);

	if (m_numWatches == 0)
	{
		return true;
	}

	bool determinedSingleton = false;

	int flags = 1 << int(EVariableWatchType::WatchModification);
	if (hasWatchersForFlag(EVariableWatchType::WatchSolved) && currentValue.isSingleton())
	{
		flags |= 1 << int(EVariableWatchType::WatchSolved);
		determinedSingleton = true;
	}
	if (hasWatchersForFlag(EVariableWatchType::WatchLowerBoundChange) && currentValue.indexOf(true) > prevValue.indexOf(true))
	{
		flags |= 1 << int(EVariableWatchType::WatchLowerBoundChange);
	}
	if (hasWatchersForFlag(EVariableWatchType::WatchUpperBoundChange) && currentValue.lastIndexOf(true) < prevValue.lastIndexOf(true))
	{
		flags |= 1 << int(EVariableWatchType::WatchUpperBoundChange);
	}

	bool result = true;
	ValueGuard<bool> guard(m_iterating, true);

	for (int segment = 0; segment < int(EVariableWatchType::NUM_WATCH_TYPES); ++segment)
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
	const ValueSet& potentialValues = db->getPotentialValues(variable);

	// Optimization for cases where only one bit is set.
	for (int segment = getFirstSingleOnSegment(), i = 0; segment < getEndSingleOnSegment(); ++segment, ++i)
	{
		if (potentialValues[m_segmentToOnKeys[i]])
		{
			continue;
		}

		if (!triggerSinks(segment, variable, prevValue, db, currentSink, triggeredTime, POOL_SINGLE_ON))
		{
			result = false;
			goto Failure;
		}
	}

	// Optimization for cases where only one bit is unset.
	// These cases will remain potentially true until the variable is solved (one value left), at which point
	// we can just check if that single bit has been removed.
	if (determinedSingleton || potentialValues.isSingleton())
	{
		determinedSingleton = true;
		for (int segment = getFirstSingleOffSegment(), i = 0; segment < getEndSingleOffSegment(); ++segment, ++i)
		{
			if (!potentialValues[m_segmentToOffKeys[i]])
			{
				continue;
			}

			if (!triggerSinks(segment, variable, prevValue, db, currentSink, triggeredTime, POOL_SINGLE_OFF))
			{
				result = false;
				goto Failure;
			}
		}
	}

	// General case: multiple bits set
	for (int segment = getFirstValueSegment(), i = 0; segment < getEndValueSegment(); ++segment, ++i)
	{
		const ValueSinkKey& key = m_segmentToValueKey[i];
		if (potentialValues.anyPossibleInRange(key.values, key.cachedMin, key.cachedMax))
		{
			sanityCheck(potentialValues.anyPossible(key.values));
			continue;
		}
		sanityCheck(!potentialValues.anyPossible(key.values));

		if (!triggerSinks(segment, variable, prevValue, db, currentSink, triggeredTime, POOL_VALUE))
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

bool GenericVariablePropagator::triggerSinks(int segment, VarID variable, const ValueSet& prevValue, IVariableDatabase* db, IVariableWatchSink** currentSink, SolverTimestamp& triggeredTime, int handlePoolIdx)
{
	bool result = true;
	bool remove = false;
	auto& seg = m_segments[segment];
	for (int i = seg.end - 1; i >= seg.start; --i)
	{
		IVariableWatchSink* sink = m_entries[i];

		// We want to skip the constraint that triggered this change
		if (sink)
		{
			*currentSink = sink;
			triggeredTime = db->getTimestamp();

			result = sink->onVariableNarrowed(db, variable, prevValue, remove);

			if (remove)
			{
				if (handlePoolIdx >= 0)
				{
					m_freeHandlesByType[handlePoolIdx].push_back(m_handles[i]);
				}
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

WatcherHandle GenericVariablePropagator::addWatcher(IVariableWatchSink* sink, EVariableWatchType watchType)
{
	++m_numWatches;
	WatcherHandle handle = createWatcherHandle(static_cast<int>(watchType));
	vxy_sanity(getTypeFromHandle(handle) == static_cast<uint32_t>(watchType));

	insertSink(int(watchType), handle, sink);
	m_flagCounts[int(watchType)]++;
	return handle;
}

bool GenericVariablePropagator::setWatcherEnabled(WatcherHandle handle, IVariableWatchSink* sink, bool enabled)
{
	auto scanAndMark = [&](int segment)
	{
		auto& seg = m_segments[segment];
		for (int i = seg.start; i < seg.end; ++i)
		{
			if (m_handles[i] == handle)
			{
				vxy_assert(!m_markedForRemoval[i]);
				if (enabled)
				{
					if (m_entries[i] != nullptr)
					{
						vxy_assert(m_entries[i] == sink);
						return false;
					}
					m_entries[i] = sink;
				}
				else
				{
					if (m_entries[i] == nullptr)
					{
						return false;
					}
					m_entries[i] = nullptr;
				}
				return true;
			}
		}

		vxy_fail();
		return false;
	};

	uint32_t watchType = getTypeFromHandle(handle);
	if (watchType < static_cast<uint32_t>(EVariableWatchType::NUM_WATCH_TYPES))
	{
		return scanAndMark(watchType);
	}
	else if (watchType == HANDLE_TYPE_VALUE)
	{
		const uint32_t handleIndex = getIndexFromHandle(handle);
		int segment = m_handleToWatchedValuesKey[handleIndex];
		vxy_assert(segment >= 0);
		segment += getFirstValueSegment();

		return scanAndMark(segment);
	}
	else if (watchType == HANDLE_TYPE_SINGLE_ON)
	{
		const uint32_t handleIndex = getIndexFromHandle(handle);
		const int watchedBit = m_handleToSingleValue[0][handleIndex];
		int segment = indexOf(m_segmentToOnKeys.begin(), m_segmentToOnKeys.end(), watchedBit);
		vxy_assert(segment >= 0);
		segment += getFirstSingleOnSegment();

		return scanAndMark(segment);
	}
	else
	{
		vxy_assert(watchType == HANDLE_TYPE_SINGLE_OFF);
		const uint32_t handleIndex = getIndexFromHandle(handle);
		const int watchedBit = m_handleToSingleValue[1][handleIndex];
		int segment = indexOf(m_segmentToOffKeys.begin(), m_segmentToOffKeys.end(), watchedBit);
		vxy_assert(segment >= 0);
		segment += getFirstSingleOffSegment();

		return scanAndMark(segment);
	}
}

WatcherHandle GenericVariablePropagator::addValueWatcher(IVariableWatchSink* sink, const ValueSet& watchValues)
{
	sanityCheck(!watchValues.isZero());

	// Get the first/last values. We use this to accelerate the check when triggering.
	int minValue = watchValues.indexOf(true);
	int maxValue = watchValues.lastIndexOf(true);

	int singleTrueBit = -1;
	int singleFalseBit = -1;

	if (minValue == maxValue)
	{
		singleTrueBit = minValue;
	}
	else
	{
		// Check if WatchValues has all values except for one.
		// If this is true, when checking triggers we only need to check this if there is one value left, and
		// it isn't the SingleFalseBit value.
		int firstFalse = watchValues.indexOf(false);
		if (watchValues.lastIndexOf(false) == firstFalse)
		{
			singleFalseBit = firstFalse;
		}
	}

	++m_numWatches;

	int segment;
	if (singleTrueBit >= 0)
	{
		segment = indexOf(m_segmentToOnKeys.begin(), m_segmentToOnKeys.end(), singleTrueBit);
		if (segment < 0)
		{
			segment = getFirstSingleOnSegment() + m_segmentToOnKeys.size();
			auto& prevSegment = m_segments[segment - 1];
			m_segments.insert(m_segments.begin() + segment, {prevSegment.end, prevSegment.end});
			m_segmentToOnKeys.push_back(singleTrueBit);
		}
		else
		{
			segment += getFirstSingleOnSegment();
		}
	}
	else if (singleFalseBit >= 0)
	{
		segment = indexOf(m_segmentToOffKeys.begin(), m_segmentToOffKeys.end(), singleFalseBit);
		if (segment < 0)
		{
			segment = getFirstSingleOffSegment() + m_segmentToOffKeys.size();
			auto& prevSegment = m_segments[segment - 1];
			m_segments.insert(m_segments.begin() + segment, {prevSegment.end, prevSegment.end});
			m_segmentToOffKeys.push_back(singleFalseBit);
		}
		else
		{
			segment += getFirstSingleOffSegment();
		}
	}
	else
	{
		ValueSinkKey key = {watchValues, minValue, maxValue};
		segment = indexOf(m_segmentToValueKey.begin(), m_segmentToValueKey.end(), key);
		if (segment < 0)
		{
			segment = m_segments.size();
			m_segments.emplace_back(m_entries.size(), m_entries.size());
			vxy_sanity(m_segmentToValueKey.size() == segment - getFirstValueSegment());
			m_segmentToValueKey.push_back(key);
		}
		else
		{
			segment += getFirstValueSegment();
		}
	}

	WatcherHandle handle;

	// Attempt to reuse handles
	const int poolIdx = singleTrueBit >= 0
		                    ? POOL_SINGLE_ON
		                    : (singleFalseBit >= 0 ? POOL_SINGLE_OFF : POOL_VALUE);

	vector<WatcherHandle>& freeHandles = m_freeHandlesByType[poolIdx];
	if (!freeHandles.empty())
	{
		handle = freeHandles.back();
		freeHandles.pop_back();

		if (singleTrueBit >= 0)
		{
			vxy_sanity(getTypeFromHandle(handle) == HANDLE_TYPE_SINGLE_ON);
			m_handleToSingleValue[0][getIndexFromHandle(handle)] = singleTrueBit;
		}
		else if (singleFalseBit >= 0)
		{
			vxy_sanity(getTypeFromHandle(handle) == HANDLE_TYPE_SINGLE_OFF);
			m_handleToSingleValue[1][getIndexFromHandle(handle)] = singleFalseBit;
		}
		else
		{
			vxy_sanity(getTypeFromHandle(handle) == HANDLE_TYPE_VALUE);
			m_handleToWatchedValuesKey[getIndexFromHandle(handle)] = segment - getFirstValueSegment();
		}
	}
	else
	{
		if (singleTrueBit >= 0)
		{
			handle = createWatcherHandle(HANDLE_TYPE_SINGLE_ON);
			const uint32_t handleIndex = getIndexFromHandle(handle);
			vxy_assert(m_handleToSingleValue[0].size() == handleIndex);
			m_handleToSingleValue[0].push_back(singleTrueBit);
		}
		else if (singleFalseBit >= 0)
		{
			handle = createWatcherHandle(HANDLE_TYPE_SINGLE_OFF);
			const uint32_t handleIndex = getIndexFromHandle(handle);
			vxy_assert(m_handleToSingleValue[1].size() == handleIndex);
			m_handleToSingleValue[1].push_back(singleFalseBit);
		}
		else
		{
			handle = createWatcherHandle(HANDLE_TYPE_VALUE);
			const uint32_t handleIndex = getIndexFromHandle(handle);
			vxy_assert(m_handleToWatchedValuesKey.size() == handleIndex);
			m_handleToWatchedValuesKey.push_back(segment - getFirstValueSegment());
		}
	}

	insertSink(segment, handle, sink);
	return handle;
}

void GenericVariablePropagator::removeWatcher(WatcherHandle handle, IVariableWatchSink* sink)
{
	if (handle == INVALID_WATCHER_HANDLE)
	{
		return;
	}

	uint32_t watchType = getTypeFromHandle(handle);
	if (watchType < static_cast<uint32_t>(EVariableWatchType::NUM_WATCH_TYPES))
	{
		removeWatcherFromList(watchType, handle);
		if (!m_iterating)
		{
			m_flagCounts[watchType]--;
		}
	}
	else
	{
		int startSegment;
		int endSegment;
		int poolIdx;

		if (watchType == HANDLE_TYPE_VALUE)
		{
			startSegment = getFirstValueSegment();
			endSegment = getEndValueSegment();
			poolIdx = POOL_VALUE;
		}
		else if (watchType == HANDLE_TYPE_SINGLE_ON)
		{
			startSegment = getFirstSingleOnSegment();
			endSegment = getEndSingleOnSegment();
			poolIdx = POOL_SINGLE_ON;
		}
		else
		{
			vxy_assert(watchType == HANDLE_TYPE_SINGLE_OFF);
			startSegment = getFirstSingleOffSegment();
			endSegment = getEndSingleOffSegment();
			poolIdx = POOL_SINGLE_OFF;
		}

		for (int segment = startSegment; segment < endSegment; ++segment)
		{
			if (removeWatcherFromList(segment, handle))
			{
				m_freeHandlesByType[poolIdx].push_back(handle);
				return;
			}
		}
	}
}

bool GenericVariablePropagator::removeWatcherFromList(int segment, WatcherHandle handle)
{
	for (int i = m_segments[segment].start; i < m_segments[segment].end; ++i)
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
			return true;
		}
	}
	return false;
}

void GenericVariablePropagator::processPendingDeletes()
{
	vxy_assert(m_anyPendingDelete);

	auto scanList = [&](int seg, int freeListIdx = -1)
	{
		for (int i = m_segments[seg].end - 1; i >= m_segments[seg].start; --i)
		{
			if (m_markedForRemoval[i])
			{
				if (freeListIdx >= 0)
				{
					m_freeHandlesByType[freeListIdx].push_back(m_handles[i]);
				}

				removeSinkAt(seg, i);
				--m_numWatches;
				if (seg < int(EVariableWatchType::NUM_WATCH_TYPES))
				{
					m_flagCounts[seg]--;
				}
			}
		}
	};

	for (int segment = 0; segment < int(EVariableWatchType::NUM_WATCH_TYPES); ++segment)
	{
		const int prevNum = m_numWatches;
		scanList(segment);
		m_flagCounts[segment] -= (prevNum - m_numWatches);
	}

	for (int segment = getFirstSingleOnSegment(); segment < getEndSingleOnSegment(); ++segment)
	{
		scanList(segment, POOL_SINGLE_ON);
	}
	for (int segment = getFirstSingleOffSegment(); segment < getEndSingleOffSegment(); ++segment)
	{
		scanList(segment, POOL_SINGLE_OFF);
	}
	for (int segment = getFirstValueSegment(); segment < getEndValueSegment(); ++segment)
	{
		scanList(segment, POOL_VALUE);
	}

	m_anyPendingDelete = false;
}
