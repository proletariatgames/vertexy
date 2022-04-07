// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once

#include "topology/Topology.h"
#include "topology/algo/TopologySearchResponse.h"

#include <EASTL/deque.h>

namespace Vertexy
{
/** Algorithm for breadth-first search through a topology */
class BreadthFirstSearchAlgorithm
{
	struct QueuedVertex
	{
		int vertex;
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
	inline bool search(const TTopology<T>& topology, int startVertex,
		const function<ETopologySearchResponse(int /*Vertex*/)>& callback)
	{
		return search(topology, startVertex, [&](int, int vertex, int, int)
		{
			return callback(vertex);
		});
	}

	/** Version that receives parent vertex in addition to visited vertex */
	template <typename T>
	inline bool search(const TTopology<T>& topology, int startVertex,
		const function<ETopologySearchResponse(int /*Vertex*/, int /*Parent*/)>& callback)
	{
		return search(topology, startVertex, [&](int, int vertex, int parent, int)
		{
			return callback(vertex, parent);
		});
	}

	/** Version that receives level (i.e. depth) in addition to vertex/parent */
	template <typename T>
	inline bool search(const TTopology<T>& topology, int startVertex,
		const function<ETopologySearchResponse(int /*Level*/, int /*Vertex*/, int /*Parent*/)>& callback)
	{
		return search(topology, startVertex, [&](int level, int vertex, int parent, int)
		{
			return callback(level, vertex, parent);
		});
	}

	/** Version that receives travelled edge index in addition to level/vertex/parent */
	template <typename T>
	inline
	bool search(const TTopology<T>& topology, int startVertex,
		const function<ETopologySearchResponse(int /*Level*/, int /*Vertex*/, int /*Parent*/, int /*EdgeIndex*/)>& callback)
	{
		// Ensure this isn't reentrant
		static bool isIterating = false;
		vxy_assert(!isIterating);
		ValueGuard<bool> iterationGuard(isIterating, true);

		vxy_assert(topology.isValidVertex(startVertex));

		m_visited.clear();
		m_visited.resize(topology.getNumVertices(), false);

		m_queue.clear();

		m_visited[startVertex] = true;
		m_queue.push_back({startVertex, -1, 0, -1});

		while (!m_queue.empty())
		{
			const int curVertex = m_queue.front().vertex;
			const int parentVertex = m_queue.front().parent;
			const int curLevel = m_queue.front().level;
			const int parentVertexEdgeIndex = m_queue.front().edgeIndex;

			m_queue.pop_front();

			ETopologySearchResponse response = callback(curLevel, curVertex, parentVertex, parentVertexEdgeIndex);
			if (response == ETopologySearchResponse::Abort)
			{
				return false;
			}
			else if (response != ETopologySearchResponse::Skip)
			{
				for (int edgeIdx = 0; edgeIdx < topology.getNumOutgoing(curVertex); ++edgeIdx)
				{
					int neighbor;
					if (topology.getOutgoingDestination(curVertex, edgeIdx, neighbor) && !m_visited[neighbor])
					{
						m_visited[neighbor] = true;
						m_queue.push_back({neighbor, curVertex, curLevel + 1, edgeIdx});
					}
				}
			}
		}

		return true;
	}

protected:
	deque<QueuedVertex> m_queue;
	vector<bool> m_visited;
};

} // namespace Vertexy