// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once

#include "ConstraintTypes.h"
#include "topology/DigraphTopology.h"

namespace csolver
{
/** Digraph that supports backtracking of edge addition/removal by timestamp */
class BacktrackingDigraphTopology : public TDigraphTopologyBase<BacktrackingDigraphTopology>
{
	using Super = TDigraphTopologyBase<BacktrackingDigraphTopology>;

public:
	bool initEdge(int nodeFrom, int nodeTo)
	{
		cs_assert(m_history.size() == 0);
		if (!hasEdge(nodeFrom, nodeTo))
		{
			Super::addEdge(nodeFrom, nodeTo);
			return true;
		}
		return false;
	}

	void addEdge(int nodeFrom, int nodeTo, int timestamp)
	{
		cs_assert_msg(!isPartiallyRewound(), "Adding edge while partially rewound");
		cs_assert(!hasEdge(nodeFrom, nodeTo));

		m_lastHistoryIdx = m_history.size();
		m_history.push_back({timestamp, nodeFrom, nodeTo});

		Super::addEdge(nodeFrom, nodeTo);
	}

	void removeEdge(int nodeFrom, int nodeTo, int timestamp)
	{
		cs_assert_msg(!isPartiallyRewound(), "Removing edge while partially rewound");
		cs_assert(hasEdge(nodeFrom, nodeTo));

		m_lastHistoryIdx = m_history.size();
		m_history.push_back({timestamp, -nodeFrom, -nodeTo});

		Super::removeEdge(nodeFrom, nodeTo);
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
			if (m_history[m_lastHistoryIdx].nodeFrom >= 0 && m_history[m_lastHistoryIdx].nodeTo >= 0)
			{
				cs_sanity(hasEdge(m_history[m_lastHistoryIdx].nodeFrom, m_history[m_lastHistoryIdx].nodeTo));
				Super::removeEdge(m_history[m_lastHistoryIdx].nodeFrom, m_history[m_lastHistoryIdx].nodeTo);
			}
			else
			{
				cs_sanity(!hasEdge(-m_history[m_lastHistoryIdx].nodeFrom, -m_history[m_lastHistoryIdx].nodeTo));
				Super::addEdge(-m_history[m_lastHistoryIdx].nodeFrom, -m_history[m_lastHistoryIdx].nodeTo);
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
			if (m_history[m_lastHistoryIdx].nodeFrom >= 0 && m_history[m_lastHistoryIdx].nodeTo >= 0)
			{
				cs_sanity(!hasEdge(m_history[m_lastHistoryIdx].nodeFrom, m_history[m_lastHistoryIdx].nodeTo));
				Super::addEdge(m_history[m_lastHistoryIdx].nodeFrom, m_history[m_lastHistoryIdx].nodeTo);
			}
			else
			{
				cs_sanity(hasEdge(-m_history[m_lastHistoryIdx].nodeFrom, -m_history[m_lastHistoryIdx].nodeTo));
				Super::removeEdge(-m_history[m_lastHistoryIdx].nodeFrom, -m_history[m_lastHistoryIdx].nodeTo);
			}
		}
	}

	inline bool isPartiallyRewound() const { return m_lastHistoryIdx < m_history.size() - 1; }

protected:
	struct HistoryRecord
	{
		int timestamp;
		int nodeFrom;
		int nodeTo;
	};

	int m_lastHistoryIdx = -1;
	vector<HistoryRecord> m_history;
};

} // namespace csolver