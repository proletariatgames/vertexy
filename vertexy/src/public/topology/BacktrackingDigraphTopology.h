// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once

#include "ConstraintTypes.h"
#include "topology/DigraphTopology.h"

namespace Vertexy
{
/** Digraph that supports backtracking of edge addition/removal by timestamp */
class BacktrackingDigraphTopology : public TDigraphTopologyBase<BacktrackingDigraphTopology>
{
	using Super = TDigraphTopologyBase<BacktrackingDigraphTopology>;

public:
	bool initEdge(int vertexFrom, int vertexTo)
	{
		vxy_assert(m_history.size() == 0);
		if (!hasEdge(vertexFrom, vertexTo))
		{
			Super::addEdge(vertexFrom, vertexTo);
			return true;
		}
		return false;
	}

	void addEdge(int vertexFrom, int vertexTo, int timestamp)
	{
		vxy_assert_msg(!isPartiallyRewound(), "Adding edge while partially rewound");
		vxy_assert(!hasEdge(vertexFrom, vertexTo));

		m_lastHistoryIdx = m_history.size();
		m_history.push_back({timestamp, vertexFrom, vertexTo});

		Super::addEdge(vertexFrom, vertexTo);
	}

	void removeEdge(int vertexFrom, int vertexTo, int timestamp)
	{
		vxy_assert_msg(!isPartiallyRewound(), "Removing edge while partially rewound");
		vxy_assert(hasEdge(vertexFrom, vertexTo));

		m_lastHistoryIdx = m_history.size();
		m_history.push_back({timestamp, -vertexFrom, -vertexTo});

		Super::removeEdge(vertexFrom, vertexTo);
	}

	void clearHistory()
	{
		m_history.clear();
	}

	inline int latestTime() const { return m_history.back().timestamp; }

	// Rewind until the given timestamp, discarding any changes after that time
	void backtrackUntil(int timestamp)
	{
		rewindUntil(timestamp);
		m_history.resize(m_lastHistoryIdx + 1);
	}

	// Rewind changes to the graph, by timestamp. FastForward will redo the changes.
	void rewindUntil(int timestamp)
	{
		while (m_lastHistoryIdx >= 0 && m_history[m_lastHistoryIdx].timestamp > timestamp)
		{
			if (m_history[m_lastHistoryIdx].vertexFrom >= 0 && m_history[m_lastHistoryIdx].vertexTo >= 0)
			{
				vxy_sanity(hasEdge(m_history[m_lastHistoryIdx].vertexFrom, m_history[m_lastHistoryIdx].vertexTo));
				Super::removeEdge(m_history[m_lastHistoryIdx].vertexFrom, m_history[m_lastHistoryIdx].vertexTo);
			}
			else
			{
				vxy_sanity(!hasEdge(-m_history[m_lastHistoryIdx].vertexFrom, -m_history[m_lastHistoryIdx].vertexTo));
				Super::addEdge(-m_history[m_lastHistoryIdx].vertexFrom, -m_history[m_lastHistoryIdx].vertexTo);
			}

			--m_lastHistoryIdx;
		}
	}

	// Undo any rewinding, moving to the latest state of the graph.
	void fastForward()
	{
		while (isPartiallyRewound())
		{
			++m_lastHistoryIdx;
			if (m_history[m_lastHistoryIdx].vertexFrom >= 0 && m_history[m_lastHistoryIdx].vertexTo >= 0)
			{
				vxy_sanity(!hasEdge(m_history[m_lastHistoryIdx].vertexFrom, m_history[m_lastHistoryIdx].vertexTo));
				Super::addEdge(m_history[m_lastHistoryIdx].vertexFrom, m_history[m_lastHistoryIdx].vertexTo);
			}
			else
			{
				vxy_sanity(hasEdge(-m_history[m_lastHistoryIdx].vertexFrom, -m_history[m_lastHistoryIdx].vertexTo));
				Super::removeEdge(-m_history[m_lastHistoryIdx].vertexFrom, -m_history[m_lastHistoryIdx].vertexTo);
			}
		}
	}

	inline bool isPartiallyRewound() const { return m_lastHistoryIdx < m_history.size() - 1; }

protected:
	struct HistoryRecord
	{
		int timestamp;
		int vertexFrom;
		int vertexTo;
	};

	int m_lastHistoryIdx = -1;
	vector<HistoryRecord> m_history;
};

} // namespace Vertexy