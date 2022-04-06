// Copyright Proletariat, Inc. All Rights Reserved.
#include "BooleanVariablePropagator.h"
#include "constraints/ISolverConstraint.h"
#include "variable/IVariableDatabase.h"

//#define sanityCheck(s)
#define sanityCheck(s) cs_sanity(s)

using namespace csolver;

BooleanVariablePropagator::BooleanVariablePropagator()
{
	m_segments.reserve(3);
	for (int i = 0; i < 3; ++i)
	{
		m_segments.push_back({0, 0});
	}
}

bool BooleanVariablePropagator::trigger(VarID variable, const ValueSet& prevValue, const ValueSet& currentValue, IVariableDatabase* db, IVariableWatchSink** currentSink, SolverTimestamp& triggeredTime)
{
	cs_assert(!m_iterating);
	cs_assert(!m_anyPendingDelete);

	sanityCheck(currentValue.isSingleton());
	ValueGuard<bool> guard(m_iterating, true);

	bool result = true;
	bool remove = false;

	// Process two sink lists: the set for when this variable becomes this specific value, and the set for
	// when this variable becomes any value.
	int segment = currentValue[0] ? 0 : 1;
	for (int i = 0; i < 2; ++i)
	{
		auto& seg = m_segments[segment];
		for (int sinkIdx = seg.end - 1; sinkIdx >= seg.start; --sinkIdx)
		{
			IVariableWatchSink* sink = m_entries[sinkIdx];

			if (sink)
			{
				*currentSink = sink;
				triggeredTime = db->getTimestamp();

				result = sink->onVariableNarrowed(db, variable, prevValue, remove);

				if (remove)
				{
					removeSinkAt(segment, sinkIdx);
					remove = false;
				}

				if (!result)
				{
					goto Failure;
				}
			}
		}

		segment = 2;
	}

Failure:
	if (m_anyPendingDelete)
	{
		processPendingDeletes();
	}

	return result;
}

WatcherHandle BooleanVariablePropagator::addWatcher(IVariableWatchSink* sink, EVariableWatchType watchType)
{
	++m_numWatches;

	int segment = -1;
	switch (watchType)
	{
	case EVariableWatchType::WatchModification:
	case EVariableWatchType::WatchSolved:
		segment = 2;
		break;
	case EVariableWatchType::WatchLowerBoundChange:
		segment = 1;
		break;
	case EVariableWatchType::WatchUpperBoundChange:
		segment = 0;
		break;
	default:
		cs_fail();
	}

	WatcherHandle handle = createWatcherHandle(segment);
	insertSink(segment, handle, sink);
	return handle;
}

WatcherHandle BooleanVariablePropagator::addValueWatcher(IVariableWatchSink* sink, const ValueSet& watchValues)
{
	sanityCheck(!watchValues.isZero());
	sanityCheck(watchValues.isSingleton());

	++m_numWatches;

	int segment = watchValues[0] ? 1 : 0;
	WatcherHandle handle = createWatcherHandle(segment);

	insertSink(segment, handle, sink);
	return handle;
}

bool BooleanVariablePropagator::setWatcherEnabled(WatcherHandle handle, IVariableWatchSink* sink, bool enabled)
{
	int segment = getSegmentFromHandle(handle);
	for (int i = m_segments[segment].start; i < m_segments[segment].end; ++i)
	{
		if (m_handles[i] == handle)
		{
			cs_assert(!m_markedForRemoval[i]);
			if (enabled)
			{
				if (m_entries[i] == nullptr)
				{
					m_entries[i] = sink;
					return true;
				}
			}
			else
			{
				if (m_entries[i] != nullptr)
				{
					cs_assert(m_entries[i] == sink);
					m_entries[i] = nullptr;
					return true;
				}
			}
			return false;
		}
	}

	cs_fail();
	return false;
}

void BooleanVariablePropagator::removeWatcher(WatcherHandle handle, IVariableWatchSink* sink)
{
	if (handle == INVALID_WATCHER_HANDLE)
	{
		return;
	}

	int segment = getSegmentFromHandle(handle);

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
			return;
		}
	}
}

void BooleanVariablePropagator::processPendingDeletes()
{
	cs_assert(m_anyPendingDelete);
	for (int segment = 0; segment < 3; ++segment)
	{
		auto& seg = m_segments[segment];
		for (int i = seg.end - 1; i >= seg.start; --i)
		{
			if (m_markedForRemoval[i])
			{
				removeSinkAt(segment, i);
				--m_numWatches;
			}
		}
	}
	m_anyPendingDelete = false;
}
