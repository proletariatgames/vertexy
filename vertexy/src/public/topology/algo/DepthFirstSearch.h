// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once

#include "ConstraintTypes.h"
#include "topology/Topology.h"
#include "topology/algo/TopologySearchResponse.h"

namespace Vertexy
{
class DepthFirstSearchAlgorithm
{
public:
	DepthFirstSearchAlgorithm(int reserveSize = 0)
	{
		m_visited.reserve(reserveSize);
	}

	template <typename T>
	inline bool search(const TTopology<T>& topology, int startVertex,
		const function<ETopologySearchResponse(int /*Vertex*/)>& callback)
	{
		return search(topology, startVertex, [&](int vertex, int, int)
		{
			return callback(vertex);
		});
	}

	template <typename T>
	inline bool search(const TTopology<T>& topology, int startVertex,
		const function<ETopologySearchResponse(int /*Vertex*/, int /*Parent*/)>& callback)
	{
		return search(topology, startVertex, [&](int vertex, int parent, int)
		{
			return callback(vertex, parent);
		});
	}

	/** Version that receives traversed edge in addition to vertex/parent */
	template <typename T>
	bool search(const TTopology<T>& topology, int startVertex,
		const function<ETopologySearchResponse(int /*Vertex*/, int /*Parent*/, int /*EdgeIndex*/)>& callback)
	{
		vxy_assert(topology.isValidVertex(startVertex));

		m_visited.clear();
		m_visited.resize(topology.getNumVertices(), false);

		m_queue.clear();

		m_visited[startVertex] = true;
		m_queue.push_back(startVertex);

		while (!m_queue.empty())
		{
			int curVertex = m_queue.back();
			m_queue.pop_back();

			for (int dir = 0; dir < topology.getNumOutgoing(curVertex); ++dir)
			{
				int neighbor;
				if (topology.getOutgoingDestination(curVertex, dir, neighbor) && !m_visited[neighbor])
				{
					ETopologySearchResponse response = callback(neighbor, curVertex, dir);
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

} // namespace Vertexy