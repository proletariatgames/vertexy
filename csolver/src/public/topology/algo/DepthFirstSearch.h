// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once

#include "ConstraintTypes.h"
#include "topology/Topology.h"
#include "topology/algo/TopologySearchResponse.h"

namespace csolver
{
class DepthFirstSearchAlgorithm
{
public:
	DepthFirstSearchAlgorithm(int reserveSize = 0)
	{
		m_visited.reserve(reserveSize);
	}

	template <typename T>
	inline bool search(const TTopology<T>& topology, int startNode,
		const function<ETopologySearchResponse(int /*Node*/)>& callback)
	{
		return search(topology, startNode, [&](int node, int, int)
		{
			return callback(node);
		});
	}

	template <typename T>
	inline bool search(const TTopology<T>& topology, int startNode,
		const function<ETopologySearchResponse(int /*Node*/, int /*Parent*/)>& callback)
	{
		return search(topology, startNode, [&](int node, int parent, int)
		{
			return callback(node, parent);
		});
	}

	/** Version that receives traversed edge in addition to node/parent */
	template <typename T>
	bool search(const TTopology<T>& topology, int startNode,
		const function<ETopologySearchResponse(int /*Node*/, int /*Parent*/, int /*EdgeIndex*/)>& callback)
	{
		cs_assert(topology.isValidNode(startNode));

		m_visited.clear();
		m_visited.resize(topology.getNumNodes(), false);

		m_queue.clear();

		m_visited[startNode] = true;
		m_queue.push_back(startNode);

		while (!m_queue.empty())
		{
			int curNode = m_queue.back();
			m_queue.pop_back();

			for (int dir = 0; dir < topology.getNumOutgoing(curNode); ++dir)
			{
				int neighbor;
				if (topology.getOutgoingDestination(curNode, dir, neighbor) && !m_visited[neighbor])
				{
					ETopologySearchResponse response = callback(neighbor, curNode, dir);
					m_visited[neighbor] = true;

					if (response == ETopologySearchResponse::Abort)
					{
						return false;
					}
					else if (response != ETopologySearchResponse::Skip)
					{
						m_queue.push_back(neighbor);
					}
				}
			}
		}

		return true;
	}

protected:
	vector<bool> m_visited;
	vector<int> m_queue;
};

} // namespace csolver