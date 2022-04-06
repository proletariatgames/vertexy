// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once

#include "topology/Topology.h"
#include "topology/algo/TopologySearchResponse.h"

#include <EASTL/deque.h>

namespace csolver
{
/** Algorithm for breadth-first search through a topology */
class BreadthFirstSearchAlgorithm
{
	struct QueuedNode
	{
		int node;
		int parent;
		int level;
		int edgeIndex;
	};

public:
	BreadthFirstSearchAlgorithm(int reserveSize = 0)
	{
		m_visited.reserve(reserveSize);
	}

	template <typename T>
	inline bool search(const TTopology<T>& topology, int startNode,
		const function<ETopologySearchResponse(int /*Node*/)>& callback)
	{
		return search(topology, startNode, [&](int, int node, int, int)
		{
			return callback(node);
		});
	}

	/** Version that receives parent node in addition to visited node */
	template <typename T>
	inline bool search(const TTopology<T>& topology, int startNode,
		const function<ETopologySearchResponse(int /*Node*/, int /*Parent*/)>& callback)
	{
		return search(topology, startNode, [&](int, int node, int parent, int)
		{
			return callback(node, parent);
		});
	}

	/** Version that receives level (i.e. depth) in addition to node/parent */
	template <typename T>
	inline bool search(const TTopology<T>& topology, int startNode,
		const function<ETopologySearchResponse(int /*Level*/, int /*Node*/, int /*Parent*/)>& callback)
	{
		return search(topology, startNode, [&](int level, int node, int parent, int)
		{
			return callback(level, node, parent);
		});
	}

	/** Version that receives travelled edge index in addition to level/node/parent */
	template <typename T>
	inline
	bool search(const TTopology<T>& topology, int startNode,
		const function<ETopologySearchResponse(int /*Level*/, int /*Node*/, int /*Parent*/, int /*EdgeIndex*/)>& callback)
	{
		// Ensure this isn't reentrant
		static bool isIterating = false;
		cs_assert(!isIterating);
		ValueGuard<bool> iterationGuard(isIterating, true);

		cs_assert(topology.isValidNode(startNode));

		m_visited.clear();
		m_visited.resize(topology.getNumNodes(), false);

		m_queue.clear();

		m_visited[startNode] = true;
		m_queue.push_back({startNode, -1, 0, -1});

		while (!m_queue.empty())
		{
			const int curNode = m_queue.front().node;
			const int parentNode = m_queue.front().parent;
			const int curLevel = m_queue.front().level;
			const int parentNodeEdgeIndex = m_queue.front().edgeIndex;

			m_queue.pop_front();

			ETopologySearchResponse response = callback(curLevel, curNode, parentNode, parentNodeEdgeIndex);
			if (response == ETopologySearchResponse::Abort)
			{
				return false;
			}
			else if (response != ETopologySearchResponse::Skip)
			{
				for (int edgeIdx = 0; edgeIdx < topology.getNumOutgoing(curNode); ++edgeIdx)
				{
					int neighbor;
					if (topology.getOutgoingDestination(curNode, edgeIdx, neighbor) && !m_visited[neighbor])
					{
						m_visited[neighbor] = true;
						m_queue.push_back({neighbor, curNode, curLevel + 1, edgeIdx});
					}
				}
			}
		}

		return true;
	}

protected:
	deque<QueuedNode> m_queue;
	vector<bool> m_visited;
};

} // namespace csolver