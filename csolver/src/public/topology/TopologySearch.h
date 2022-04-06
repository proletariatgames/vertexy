// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once
#include <EASTL/functional.h>
#include "ConstraintTypes.h"
#include "topology/algo/DepthFirstSearch.h"
#include "topology/algo/TopologySearchResponse.h"
#include "topology/algo/tarjan.h"

namespace csolver
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
		depthFirstSearchAlgorithm.search(topology, start, [&](int node)
		{
			if (node == end)
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
	 * The output is a list where each element corresponds to the input node at the same index,
	 * and the value identifies the representative node of the SCC the node belongs to.
	 */
	template <typename Topo>
	static void findStronglyConnectedComponents(const Topo& topology, vector<int>& output)
	{
		auto getNeighborsFn = [&](int node)
		{
			return topology.getNeighbors(node);
		};

		TarjanAlgorithm tarjanAlgorithm;
		tarjanAlgorithm.findStronglyConnectedComponents(topology.getNumNodes(), getNeighborsFn, output);
	}

	/** Call Callback for each edge discovered in the given graph */
	template <typename Topo, typename CallbackFn>
	inline static void iterateAllEdges(const shared_ptr<Topo>& topology, const function<void(int /*SourceNode*/, int /*EdgeIndex*/, int /*EndNode*/)>& callback)
	{
		iterateAllEdges(*topology.Get(), callback);
	}

	template <typename Topo>
	static void iterateAllEdges(const Topo& topology, const function<void(int /*SourceNode*/, int /*EdgeIndex*/, int /*EndNode*/)>& callback)
	{
		const int numNodes = topology.getNumNodes();
		for (int sourceNode = 0; sourceNode < numNodes; ++sourceNode)
		{
			const int numOutgoing = topology.getNumOutgoing(sourceNode);
			for (int edgeIdx = 0; edgeIdx < numOutgoing; ++edgeIdx)
			{
				int destNode;
				if (topology.getOutgoingDestination(sourceNode, edgeIdx, destNode))
				{
					cs_assert(destNode != sourceNode);
					callback(sourceNode, edgeIdx, destNode);
				}
			}
		}
	}
};

} // namespace csolver