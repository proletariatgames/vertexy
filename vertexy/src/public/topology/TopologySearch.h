// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once
#include <EASTL/functional.h>
#include "ConstraintTypes.h"
#include "topology/algo/DepthFirstSearch.h"
#include "topology/algo/TopologySearchResponse.h"
#include "topology/algo/tarjan.h"

namespace Vertexy
{

/**
 * Various search algorithms for TTopology classes
 */
class TopologySearchAlgorithm
{
	TopologySearchAlgorithm() = delete;

public:
	/** Returns true if there is a path between Start and End. */
	template <typename Topo>
	inline static bool canReach(const shared_ptr<Topo>& topology, int start, int end)
	{
		return canReach(*topology.get(), start, end);
	}

	/** Returns true if there is a path between Start and End. */
	template <typename Topo>
	static bool canReach(const Topo& topology, int start, int end)
	{
		bool reached = false;
		DepthFirstSearchAlgorithm depthFirstSearchAlgorithm;
		depthFirstSearchAlgorithm.search(topology, start, [&](int vertex)
		{
			if (vertex == end)
			{
				reached = true;
				return ETopologySearchResponse::Abort;
			}
			return ETopologySearchResponse::Continue;
		});

		return reached;
	}

	/**
	 * Find all strongly-connected components (SCCs).
	 * See https://en.wikipedia.org/wiki/Strongly_connected_component
	 *
	 * The output is a list where each element corresponds to the input vertex at the same index,
	 * and the value identifies the representative vertex of the SCC the vertex belongs to.
	 */
	template <typename T>
	static void findStronglyConnectedComponents(const TTopology<T>& topology, vector<int>& output)
	{
		auto writeSCCs = [&](int level, auto& it)
		{
			for (; it; ++it)
			{
				int sccMember = *it;
				output[sccMember] = it.representative();
			}
		};
		findStronglyConnectedComponents(topology, writeSCCs);
	}

	template<typename T, typename S>
	static void findStronglyConnectedComponents(const TTopology<T>& topology, S&& callback)
	{
		auto getNeighborsFn = [&](int vertex, auto&& visitor)
		{
			auto neighbors = topology.getNeighbors(vertex);
			for (auto it = neighbors.begin(); it != neighbors.end(); ++it)
			{
				visitor(*it);
			}
		};

		TarjanAlgorithm tarjanAlgorithm;
		tarjanAlgorithm.findStronglyConnectedComponents(topology.getNumVertices(), getNeighborsFn, callback);
	}

	/** Call Callback for each edge discovered in the given graph */
	template <typename Topo, typename CallbackFn>
	inline static void iterateAllEdges(const shared_ptr<Topo>& topology, const function<void(int /*SourceVertex*/, int /*EdgeIndex*/, int /*EndVertex*/)>& callback)
	{
		iterateAllEdges(*topology.Get(), callback);
	}

	template <typename Topo>
	static void iterateAllEdges(const Topo& topology, const function<void(int /*SourceVertex*/, int /*EdgeIndex*/, int /*EndVertex*/)>& callback)
	{
		const int numVertices = topology.getNumVertices();
		for (int sourceVertex = 0; sourceVertex < numVertices; ++sourceVertex)
		{
			const int numOutgoing = topology.getNumOutgoing(sourceVertex);
			for (int edgeIdx = 0; edgeIdx < numOutgoing; ++edgeIdx)
			{
				int destVertex;
				if (topology.getOutgoingDestination(sourceVertex, edgeIdx, destVertex))
				{
					vxy_assert(destVertex != sourceVertex);
					callback(sourceVertex, edgeIdx, destVertex);
				}
			}
		}
	}
};

} // namespace Vertexy