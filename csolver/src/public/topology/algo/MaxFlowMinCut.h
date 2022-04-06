// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once

#include "ConstraintTypes.h"
#include <EASTL/deque.h>
#include <EASTL/functional.h>

namespace csolver
{
/** For specifying edge connectivity and capacity data for TMaxFlowMinCutAlgorithm */
template <typename CapacityType>
struct TFlowGraphEdge
{
	// The node this edge ends at.
	int endNode;
	// The index of the reversed version of this edge. Must always be valid!
	int reverseEdgeIndex;
	// The flow capacity of the edge in this direction (toward EndNode).
	CapacityType capacity;
};

// For each node, the index of the nodes first edge and one past last edge, in the corresponding edge array
using FlowGraphLookupMap = vector<tuple<int, int>>;

/** For finding the maximum current flow within a topology, given a source and sink. Can also compute the
 *  graph partition separating source/sink that cuts through the edges with least flow.
 *
 *  For implementation details see:
 *		"An Experimental Comparison of Min-Cut/Max-Flow Algorithms for Energy Minimization in Vision", Boykov/Kolmogorov
 *		https://www.csd.uwo.ca/~yboykov/Papers/pami04.pdf
 */
template <typename CapacityType>
class TMaxFlowMinCutAlgorithm
{
	struct MinCutNodeInfo
	{
		int backEdge = -1;
		int stamp = -1;
		int dist = -1;
		bool fromSource = false;
		bool orphaned = false;
		bool active = false;
	};

public:
	TMaxFlowMinCutAlgorithm()
	{
	}

	template <typename T>
	CapacityType getMaxFlow(const TTopology<T>& topology, int inSource, int inSink,
		const vector<TFlowGraphEdge<CapacityType>>& edges, const FlowGraphLookupMap& map,
		vector<tuple<int, int>>* outMinCutEdges = nullptr)
	{
		m_source = inSource;
		m_sink = inSink;
		cs_assert(m_source != m_sink);

		//
		// Create the residual edge capacities, initialized to the initial capacities.
		//

		m_edgeCapacities.clear();
		m_edgeCapacities.reserve(edges.size());
		for (int i = 0; i < edges.size(); ++i)
		{
			m_edgeCapacities.push_back(edges[i].capacity);
		}

		m_nodeInfos.clear();
		m_nodeInfos.resize(topology.getNumNodes());

		m_nodeQueue.clear();
		m_orphanQueue.clear();

		// Initialize the queue with both source and sink. We will search breadth-first from each one in parallel,
		// until they meet.

		const int terminalEdge = edges.size();

		m_nodeInfos[m_source].fromSource = true;
		m_nodeInfos[m_source].active = true;
		m_nodeInfos[m_source].backEdge = terminalEdge;
		m_nodeQueue.push_back(m_source);

		m_nodeInfos[m_sink].fromSource = false;
		m_nodeInfos[m_sink].active = true;
		m_nodeInfos[m_sink].backEdge = terminalEdge;
		m_nodeQueue.push_back(m_sink);

		//
		// Main loop
		//

		m_stamp = 0;
		m_maxFlow = 0;

		int activeNode;
		while ((activeNode = getNextNode()) >= 0)
		{
			//
			// Process edges of next queued node. If the path from source<->sink is formed, ConnectingEdge will be
			// set to the edge where they meet.
			//

			int connectingEdge = -1;

			MinCutNodeInfo& activeNodeInfo = m_nodeInfos[activeNode];
			const int originNode = activeNodeInfo.fromSource ? m_source : m_sink;

			for (int edge = get<0>(map[activeNode]); edge < get<1>(map[activeNode]); ++edge)
			{
				const int revEdge = edges[edge].reverseEdgeIndex;
				cs_assert(edges[revEdge].endNode == activeNode);
				const int flowEdge = activeNodeInfo.fromSource ? edge : revEdge;

				if (m_edgeCapacities[flowEdge] > 0)
				{
					const int nextNode = edges[edge].endNode;
					if (nextNode == originNode)
					{
						continue;
					}

					MinCutNodeInfo& nextNodeInfo = m_nodeInfos[nextNode];
					if (nextNodeInfo.backEdge < 0)
					{
						// If this is part of the source search tree, then BackEdge will point toward source.
						// Otherwise, it is part of the sink search tree and will point toward sink.
						nextNodeInfo.backEdge = revEdge;
						nextNodeInfo.orphaned = false;
						nextNodeInfo.fromSource = activeNodeInfo.fromSource;
						nextNodeInfo.stamp = activeNodeInfo.stamp;
						nextNodeInfo.dist = activeNodeInfo.dist + 1;

						if (!nextNodeInfo.active)
						{
							nextNodeInfo.active = true;
							m_nodeQueue.push_back(nextNode);
						}
					}
					else if (nextNodeInfo.fromSource != activeNodeInfo.fromSource)
					{
						// ConnectingEdge will always point toward sink
						connectingEdge = flowEdge;
						break;
					}
					else if (nextNodeInfo.stamp <= activeNodeInfo.stamp && nextNodeInfo.dist > activeNodeInfo.dist)
					{
						// attempt to keep shortest-path
						cs_assert(!nextNodeInfo.orphaned);
						nextNodeInfo.backEdge = revEdge;
						nextNodeInfo.stamp = activeNodeInfo.stamp;
						nextNodeInfo.dist = activeNodeInfo.dist + 1;
					}
				}
			}

			// increase stamp to invalidate cached path info
			++m_stamp;

			if (connectingEdge > 0)
			{
				// Re-add the current node to active list - it may have more edges that need to be processed.
				m_nodeQueue.push_back(activeNode);
				m_nodeInfos[activeNode].active = true;

				// Push the maximum flow through this path, reducing residual capacity of each traversed.
				pushFlow(connectingEdge, edges);

				// Attempt to adopt orphans, connecting them with incoming edges that still have capacity.
				while (!m_orphanQueue.empty())
				{
					int orphan = m_orphanQueue.front();
					m_orphanQueue.pop_front();

					cs_assert(m_nodeInfos[orphan].orphaned);
					processOrphan(orphan, edges, map);
				}
			}
		}

		//
		// The queue is empty, so max flow has been found. Find the cutset.
		//

		m_computedCut = false;
		if (outMinCutEdges != nullptr)
		{
			outMinCutEdges->clear();
			computeCut(edges, map, [&](int from, int to)
			{
				if (topology.hasEdge(from, to))
				{
					cs_sanity((!contains(outMinCutEdges->begin(), outMinCutEdges->end(), make_tuple(from, to))));
					outMinCutEdges->push_back(make_tuple(from, to));
				}
			});
		}

		return m_maxFlow;
	}

	// Can be called after GetMaxFlow() completes. Returns true if this node is on the same side of the edge cut as
	// the sink node
	bool onSinkSide(int nodeIndex, const vector<TFlowGraphEdge<CapacityType>>& edges, const FlowGraphLookupMap& map) const
	{
		if (nodeIndex == m_source)
		{
			return true;
		}
		else if (nodeIndex == m_sink)
		{
			return false;
		}

		if (!m_computedCut && m_nodeInfos[nodeIndex].backEdge < 0)
		{
			computeCut(edges, map, [](int from, int to)
			{
			});
		}

		return (m_nodeInfos[nodeIndex].backEdge >= 0 && !m_nodeInfos[nodeIndex].fromSource);
	}

protected:
	inline int getNextNode()
	{
		while (!m_nodeQueue.empty())
		{
			int node = m_nodeQueue.front();
			m_nodeQueue.pop_front();

			MinCutNodeInfo& nodeInfo = m_nodeInfos[node];
			cs_assert(nodeInfo.active);
			nodeInfo.active = false;
			if (nodeInfo.backEdge >= 0)
			{
				return node;
			}
		}
		return -1;
	}

	// Finds the minimum flow along the found path, then subtracts that flow from each edge along the path.
	void pushFlow(int connectingEdge, const vector<TFlowGraphEdge<CapacityType>>& edges)
	{
		const int terminalEdge = edges.size();

		//
		// Sink and source are now connected - ConnectingEdge is connecting both search trees, pointing from Source toward Sink.
		//

		int nodes[2] = {edges[edges[connectingEdge].reverseEdgeIndex].endNode, edges[connectingEdge].endNode};

		//
		// Find the minimum flow along the path.
		//

		int minFlow = m_edgeCapacities[connectingEdge];
		for (bool towardSink : {true, false})
		{
			int edge;
			for (int n = nodes[towardSink]; m_nodeInfos[n].backEdge != terminalEdge; n = edges[edge].endNode)
			{
				const auto& ni = m_nodeInfos[n];
				cs_assert(ni.fromSource != towardSink);

				edge = ni.backEdge;
				const int revEdge = edges[edge].reverseEdgeIndex;

				const CapacityType edgeFlow = towardSink ? m_edgeCapacities[edge] : m_edgeCapacities[revEdge];
				cs_assert(edgeFlow > 0);
				minFlow = min(minFlow, edgeFlow);
			}
		}
		cs_assert(minFlow > 0);
		m_maxFlow += minFlow;

		//
		// Now subtract the flow through this path (storing total in the opposite edge)
		//
		// Also mark any nodes that have become orphaned, i.e. the edge leading to them in the path has run out
		// of capacity.
		//

		cs_assert(m_edgeCapacities[connectingEdge] >= minFlow);
		m_edgeCapacities[connectingEdge] -= minFlow;
		m_edgeCapacities[edges[connectingEdge].reverseEdgeIndex] += minFlow;

		for (bool towardSink : {true, false})
		{
			int edge;
			for (int n = nodes[towardSink]; m_nodeInfos[n].backEdge != terminalEdge; n = edges[edge].endNode)
			{
				auto& ni = m_nodeInfos[n];
				cs_assert(ni.fromSource != towardSink);

				edge = ni.backEdge;

				const int revEdge = edges[edge].reverseEdgeIndex;

				const int flowEdge = towardSink ? edge : revEdge;
				const int residualEdge = towardSink ? revEdge : edge;

				cs_assert(m_edgeCapacities[flowEdge] >= minFlow);
				m_edgeCapacities[flowEdge] -= minFlow;
				m_edgeCapacities[residualEdge] += minFlow;

				// Add to list of orphans if this edge has run out of capacity
				if (m_edgeCapacities[flowEdge] <= 0)
				{
					cs_assert(!m_nodeInfos[n].orphaned);
					m_orphanQueue.push_front(n);
					m_nodeInfos[n].backEdge = -1;
					m_nodeInfos[n].orphaned = true;
				}
			}
		}
	}

	// Processes a node that is connected through an edge that no longer has any capacity. See if it can connect
	// with any neighbor through an edge that still has capacity. Otherwise, ensure neighbors are activated/orphaned.
	void processOrphan(int orphan, const vector<TFlowGraphEdge<CapacityType>>& edges, const FlowGraphLookupMap& map)
	{
		const int terminalEdge = edges.size();

		//
		// Look at neighbors of orphan to see if any still have capacity. Pick the neighbor that has the
		// least distance from its origin (i.e. source or sink).
		//

		bool orphanFromSource = m_nodeInfos[orphan].fromSource;
		int minDistance = INT_MAX;
		int bestEdge = -1;
		for (int edge = get<0>(map[orphan]), end = get<1>(map[orphan]); edge < end; ++edge)
		{
			const int revEdge = edges[edge].reverseEdgeIndex;
			const int flowEdge = orphanFromSource ? revEdge : edge;
			if (m_edgeCapacities[flowEdge] > 0)
			{
				const int neighborNode = edges[edge].endNode;
				if (m_nodeInfos[neighborNode].fromSource != orphanFromSource)
				{
					// wrong search direction
					continue;
				}

				if (m_nodeInfos[neighborNode].backEdge < 0)
				{
					// not in process list yet
					continue;
				}

				// Follow BackEdge path backward to see if this is still a connected to its origin: it might be orphaned earlier in path
				int dist = 0;
				bool validPath = false;
				for (int curEdge = edge, curNode = edges[curEdge].endNode; !m_nodeInfos[curNode].orphaned; curNode = edges[curEdge].endNode)
				{
					auto& curNodeInfo = m_nodeInfos[curNode];
					if (curNodeInfo.stamp == m_stamp)
					{
						// We found this to be a valid path to origin already (see "mark edges" section below)
						dist += curNodeInfo.dist;
						validPath = true;
						break;
					}

					++dist;
					curEdge = curNodeInfo.backEdge;
					if (curEdge == terminalEdge)
					{
						curNodeInfo.stamp = m_stamp;
						curNodeInfo.dist = 1;
						validPath = true;
						break;
					}
					cs_assert(m_nodeInfos[edges[curEdge].endNode].fromSource == curNodeInfo.fromSource);
				}

				if (validPath)
				{
					if (dist < minDistance)
					{
						minDistance = dist;
						bestEdge = edge;
					}

					// Mark edges to speed up other orphan checks that share a subset of the path
					for (int curNode = edges[edge].endNode; m_nodeInfos[curNode].stamp != m_stamp; curNode = edges[m_nodeInfos[curNode].backEdge].endNode)
					{
						m_nodeInfos[curNode].stamp = m_stamp;
						m_nodeInfos[curNode].dist = dist;
						--dist;
					}
				}
			}
		}

		if (bestEdge >= 0)
		{
			// Found a viable neighbor; relink the orphan.
			m_nodeInfos[orphan].orphaned = false;

			m_nodeInfos[orphan].backEdge = bestEdge;
			m_nodeInfos[orphan].stamp = m_stamp;
			m_nodeInfos[orphan].dist = minDistance + 1;
		}
		else
		{
			// No neighbors with capacity left, so this node is now inactive.
			// For each neighbor, if there is still capacity, add that neighbor to the active list.
			// If the parent edge of the neighbor points to us, then add it to orphan list.
			for (int edge = get<0>(map[orphan]), end = get<1>(map[orphan]); edge < end; ++edge)
			{
				const int nextNode = edges[edge].endNode;
				auto& nextNodeInfo = m_nodeInfos[nextNode];
				if (nextNodeInfo.backEdge < 0 || nextNodeInfo.fromSource != orphanFromSource)
				{
					continue;
				}
				const int revEdge = edges[edge].reverseEdgeIndex;
				const int flowEdge = orphanFromSource ? revEdge : edge;

				if (m_edgeCapacities[flowEdge] > 0 && !nextNodeInfo.active)
				{
					nextNodeInfo.active = true;
					m_nodeQueue.push_back(nextNode);
				}
				if (nextNodeInfo.backEdge != terminalEdge && !nextNodeInfo.orphaned && edges[nextNodeInfo.backEdge].endNode == orphan)
				{
					nextNodeInfo.backEdge = -1;
					nextNodeInfo.orphaned = true;
					m_orphanQueue.push_back(nextNode);
				}
			}

			cs_assert(m_nodeInfos[orphan].backEdge < 0);
		}
	}


	void computeCut(const vector<TFlowGraphEdge<CapacityType>>& edges, const FlowGraphLookupMap& edgeMap, const function<void(int, int)>& cutEdgeCallback) const
	{
		if (m_computedCut)
		{
			return;
		}

		const int terminalEdge = edges.size();
		m_computedCut = true;

		// Traverse graph from sink->source to find first-found edges that have no more capacity.
		// Those form the minimal cut-set.
		cs_assert(!m_nodeInfos[m_sink].active);
		m_nodeInfos[m_sink].active = true;
		m_nodeQueue.push_back(m_sink);

		while (!m_nodeQueue.empty())
		{
			int curNode = m_nodeQueue.front();
			m_nodeQueue.pop_front();

			cs_assert(m_nodeInfos[curNode].active);

			for (int edge = get<0>(edgeMap[curNode]); edge < get<1>(edgeMap[curNode]); ++edge)
			{
				const int nextNode = edges[edge].endNode;
				if (!m_nodeInfos[nextNode].active)
				{
					m_nodeInfos[nextNode].fromSource = false;
					m_nodeInfos[nextNode].backEdge = terminalEdge;

					if (m_edgeCapacities[edges[edge].reverseEdgeIndex] > 0)
					{
						m_nodeInfos[nextNode].active = true;
						m_nodeQueue.push_back(nextNode);
					}
					else
					{
						cutEdgeCallback(nextNode, curNode);
					}
				}
			}
		}
	}

	int m_source = -1;
	int m_sink = -1;
	CapacityType m_maxFlow = 0;

	int m_stamp = 0;
	vector<CapacityType> m_edgeCapacities;
	deque<int> m_orphanQueue;

	mutable vector<MinCutNodeInfo> m_nodeInfos;
	mutable deque<int> m_nodeQueue;
	mutable bool m_computedCut = false;
};

} // namespace csolver