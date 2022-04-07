// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once

#include "variable/IVariablePropagator.h"

namespace Vertexy
{

/** Abstract base class for propagators that work with segmented arrays to manage separate lists */
class SegmentedVariablePropagator : public IVariablePropagator
{
protected:
	struct SinkSegment
	{
		SinkSegment(int start, int end)
			: start(start)
			, end(end)
		{
		}

		int start;
		int end;
	};

	int insertSink(int segment, WatcherHandle handle, IVariableWatchSink* sink);
	void removeSinkAt(int segment, int i);

	inline bool withinSegment(int segment, int index) const
	{
		return index >= m_segments[segment].start && index < m_segments[segment].end;
	}

	fixed_vector<SinkSegment, 8> m_segments;

	// We lay out memory as "struct of arrays" to optimize cache locality, particularly during iteration
	// Each of these arrays should always be the same size.
	vector<IVariableWatchSink*> m_entries;
	vector<WatcherHandle> m_handles;
	vector<bool> m_markedForRemoval;
};

} // namespace Vertexy