// Copyright Proletariat, Inc. All Rights Reserved.
#include "SegmentedVariablePropagator.h"

using namespace Vertexy;

int SegmentedVariablePropagator::insertSink(int segment, WatcherHandle handle, IVariableWatchSink* sink)
{
	auto& seg = m_segments[segment];
	vxy_assert(seg.end <= m_entries.size());
	vxy_assert(segment == m_segments.size()-1 || m_segments[segment+1].start >= seg.end);

	// See if we have more room to insert the sink within this segment
	int slack = (segment == m_segments.size() - 1)
		            ? (m_entries.size() - seg.end)
		            : (m_segments[segment + 1].start - seg.end);

	if (slack == 0)
	{
		// Need to make room; push everything down.
		m_entries.insert(m_entries.begin() + seg.end, sink);
		m_handles.insert(m_handles.begin() + seg.end, handle);
		m_markedForRemoval.insert(m_markedForRemoval.begin() + seg.end, false);

		// Adjust offsets of later segments
		for (int i = segment + 1; i < m_segments.size(); ++i)
		{
			++m_segments[i].start;
			++m_segments[i].end;
		}
	}
	else
	{
		// room available
		m_entries[seg.end] = sink;
		m_handles[seg.end] = handle;
		m_markedForRemoval[seg.end] = false;
	}

	vxy_sanity(m_entries[seg.end] == sink);
	vxy_sanity(m_handles[seg.end] == handle);

	++seg.end;
	return seg.end;
}

void SegmentedVariablePropagator::removeSinkAt(int segment, int i)
{
	vxy_assert(withinSegment(segment, i));
	vxy_assert(m_segments[segment].end > m_segments[segment].start);

	// Move the entry at the back of the list to this position, then back up our end marker
	const int last = m_segments[segment].end - 1;
	if (i != last)
	{
		vxy_sanity(m_handles[last] != INVALID_WATCHER_HANDLE);
		m_entries[i] = m_entries[last];
		m_handles[i] = m_handles[last];
		m_markedForRemoval[i] = m_markedForRemoval[last];

		m_entries[last] = nullptr;
		m_handles[last] = INVALID_WATCHER_HANDLE;
		m_markedForRemoval[last] = false;
	}

	--m_segments[segment].end;
	vxy_assert(m_segments[segment].end >= m_segments[segment].start);
}
