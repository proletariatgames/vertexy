// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once

#include "ConstraintTypes.h"
#include <EASTL/deque.h>
#include <EASTL/functional.h>

namespace Vertexy
{
/** For specifying edge connectivity and capacity data for TMaxFlowMinCutAlgorithm */
template <typename CapacityType>
struct TFlowGraphEdge
{
	// The vertex this edge ends at.
	int endVertex;
	// The index of the reversed version of this edge. Must always be valid!
	int reverseEdgeIndex;
	// The flow capacity of the edge in this direction (toward endVertex).
	CapacityType capacity;
};

// For each vertex, the index of the vertex first edge and one past last edge, in the corresponding edge array
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
	struct MinCutVertexInfo
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
		vxy_assert(m_source != m_sink);

		//
		// Create the residual edge capacities, initialized to the initial capacities.
		//

		m_edgeCapacities.clear();
		m_edgeCapacities.reserve(edges.size());
		for (int i = 0; i < edges.size(); ++i)
		{
			m_edgeCapacities.push_back(edges[i].capacity);
		}

		m_vertexInfos.clear();
		m_vertexInfos.resize(topology.getNumVertices());

		m_vertexQueue.clear();
		m_orphanQueue.clear();

		// Initialize the queue with both source and sink. We will search breadth-first from each one in parallel,
		// until they meet.

		const int terminalEdge = edges.size();

		m_vertexInfos[m_source].fromSource = true;
		m_vertexInfos[m_source].active = true;
		m_vertexInfos[m_source].backEdge = terminalEdge;
		m_vertexQueue.push_back(m_source);

		m_vertexInfos[m_sink].fromSource = false;
		m_vertexInfos[m_sink].active = true;
		m_vertexInfos[m_sink].backEdge = terminalEdge;
		m_vertexQueue.push_back(m_sink);

		//
		// Main loop
		//

		m_stamp = 0;
		m_maxFlow = 0;

		int activeVertex;
		while ((activeVertex = getNextVertex()) >= 0)
		{
			//
			// Process edges of next queued vertex. If the path from source<->sink is formed, ConnectingEdge will be
			// set to the edge where they meet.
			//

			int connectingEdge = -1;

			MinCutVertexInfo& activeVertInfo = m_vertexInfos[activeVertex];
			const int originVertex = activeVertInfo.fromSource ? m_source : m_sink;

			for (int edge = get<0>(map[activeVertex]); edge < get<1>(map[activeVertex]); ++edge)
			{
				const int revEdge = edges[edge].reverseEdgeIndex;
				vxy_assert(edges[revEdge].endVertex == activeVertex);
				const int flowEdge = activeVertInfo.fromSource ? edge : revEdge;

				if (m_edgeCapacities[flowEdge] > 0)
				{
					const int nextVertex = edges[edge].endVertex;
					if (nextVertex == originVertex)
					{
						continue;
					}

					MinCutVertexInfo& nextVertexInfo = m_vertexInfos[nextVertex];
					if (nextVertexInfo.backEdge < 0)
					{
						// If this is part of the source search tree, then BackEdge will point toward source.
						// Otherwise, it is part of the sink search tree and will point toward sink.
						nextVertexInfo.backEdge = revEdge;
						nextVertexInfo.orphaned = false;
						nextVertexInfo.fromSource = activeVertInfo.fromSource;
						nextVertexInfo.stamp = activeVertInfo.stamp;
						nextVertexInfo.dist = activeVertInfo.dist + 1;

						if (!nextVertexInfo.active)
						{
							nextVertexInfo.active = true;
							m_vertexQueue.push_back(nextVertex);
						}
					}
					else if (nextVertexInfo.fromSource != activeVertInfo.fromSource)
					{
						// ConnectingEdge will always point toward sink
						connectingEdge = flowEdge;
						break;
					}
					else if (nextVertexInfo.stamp <= activeVertInfo.stamp && nextVertexInfo.dist > activeVertInfo.dist)
					{
						// attempt to keep shortest-path
						vxy_assert(!nextVertexInfo.orphaned);
						nextVertexInfo.backEdge = revEdge;
						nextVertexInfo.stamp = activeVertInfo.stamp;
						nextVertexInfo.dist = activeVertInfo.dist + 1;
					}
				}
			}

			// increase stamp to invalidate cached path info
			++m_stamp;

			if (connectingEdge > 0)
			{
				// Re-add the current vertex to active list - it may have more edges that need to be processed.
				m_vertexQueue.push_back(activeVertex);
				m_vertexInfos[activeVertex].active = true;

				// Push the maximum flow through this path, reducing residual capacity of each traversed.
				pushFlow(connectingEdge, edges);

				// Attempt to adopt orphans, connecting them with incoming edges that still have capacity.
				while (!m_orphanQueue.empty())
				{
					int orphan = m_orphanQueue.front();
					m_orphanQueue.pop_front();

					vxy_assert(m_vertexInfos[orphan].orphaned);
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
					vxy_sanity((!contains(outMinCutEdges->begin(), outMinCutEdges->end(), make_tuple(from, to))));
					outMinCutEdges->push_back(make_tuple(from, to));
				}
			});
		}

		return m_maxFlow;
	}

	// Can be called after GetMaxFlow() completes. Returns true if this vertex is on the same side of the edge cut as
	// the sink vertex
	bool onSinkSide(int vertexIndex, const vector<TFlowGraphEdge<CapacityType>>& edges, const FlowGraphLookupMap& map) const
	{
		if (vertexIndex == m_source)
		{
			return true;
		}
		else if (vertexIndex == m_sink)
		{
			return false;
		}

		if (!m_computedCut && m_vertexInfos[vertexIndex].backEdge < 0)
		{
			computeCut(edges, map, [](int from, int to)
			{
			});
		}

		return (m_vertexInfos[vertexIndex].backEdge >= 0 && !m_vertexInfos[vertexIndex].fromSource);
	}

protected:
	inline int getNextVertex()
	{
		while (!m_vertexQueue.empty())
		{
			int vertex = m_vertexQueue.front();
			m_vertexQueue.pop_front();

			MinCutVertexInfo& vertexInfo = m_vertexInfos[vertex];
			vxy_assert(vertexInfo.active);
			vertexInfo.active = false;
			if (vertexInfo.backEdge >= 0)
			{
				return vertex;
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

		int vertices[2] = {edges[edges[connectingEdge].reverseEdgeIndex].endVertex, edges[connectingEdge].endVertex};

		//
		// Find the minimum flow along the path.
		//

		int minFlow = m_edgeCapacities[connectingEdge];
		for (bool towardSink : {true, false})
		{
			int edge;
			for (int n = vertices[towardSink]; m_vertexInfos[n].backEdge != terminalEdge; n = edges[edge].endVertex)
			{
				const auto& ni = m_vertexInfos[n];
				vxy_assert(ni.fromSource != towardSink);

				edge = ni.backEdge;
				const int revEdge = edges[edge].reverseEdgeIndex;

				const CapacityType edgeFlow = towardSink ? m_edgeCapacities[edge] : m_edgeCapacities[revEdge];
				vxy_assert(edgeFlow > 0);
				minFlow = min(minFlow, edgeFlow);
			}
		}
		vxy_assert(minFlow > 0);
		m_maxFlow += minFlow;

		//
		// Now subtract the flow through this path (storing total in the opposite edge)
		//
		// Also mark any vertices that have become orphaned, i.e. the edge leading to them in the path has run out
		// of capacity.
		//

		vxy_assert(m_edgeCapacities[connectingEdge] >= minFlow);
		m_edgeCapacities[connectingEdge] -= minFlow;
		m_edgeCapacities[edges[connectingEdge].reverseEdgeIndex] += minFlow;

		for (bool towardSink : {true, false})
		{
			int edge;
			for (int n = vertices[towardSink]; m_vertexInfos[n].backEdge != terminalEdge; n = edges[edge].endVertex)
			{
				auto& ni = m_vertexInfos[n];
				vxy_assert(ni.fromSource != towardSink);

				edge = ni.backEdge;

				const int revEdge = edges[edge].reverseEdgeIndex;

				const int flowEdge = towardSink ? edge : revEdge;
				const int residualEdge = towardSink ? revEdge : edge;

				vxy_assert(m_edgeCapacities[flowEdge] >= minFlow);
				m_edgeCapacities[flowEdge] -= minFlow;
				m_edgeCapacities[residualEdge] += minFlow;

				// Add to list of orphans if this edge has run out of capacity
				if (m_edgeCapacities[flowEdge] <= 0)
				{
					vxy_assert(!m_vertexInfos[n].orphaned);
					m_orphanQueue.push_front(n);
					m_vertexInfos[n].backEdge = -1;
					m_vertexInfos[n].orphaned = true;
				}
			}
		}
	}

	// Processes a vertex that is connected through an edge that no longer has any capacity. See if it can connect
	// with any neighbor through an edge that still has capacity. Otherwise, ensure neighbors are activated/orphaned.
	void processOrphan(int orphan, const vector<TFlowGraphEdge<CapacityType>>& edges, const FlowGraphLookupMap& map)
	{
		const int terminalEdge = edges.size();

		//
		// Look at neighbors of orphan to see if any still have capacity. Pick the neighbor that has the
		// least distance from its origin (i.e. source or sink).
		//

		bool orphanFromSource = m_vertexInfos[orphan].fromSource;
		int minDistance = INT_MAX;
		int bestEdge = -1;
		for (int edge = get<0>(map[orphan]), end = get<1>(map[orphan]); edge < end; ++edge)
		{
			const int revEdge = edges[edge].reverseEdgeIndex;
			const int flowEdge = orphanFromSource ? revEdge : edge;
			if (m_edgeCapacities[flowEdge] > 0)
			{
				const int neighborVertex = edges[edge].endVertex;
				if (m_vertexInfos[neighborVertex].fromSource != orphanFromSource)
				{
					// wrong search direction
					continue;
				}

				if (m_vertexInfos[neighborVertex].backEdge < 0)
				{
					// not in process list yet
					continue;
				}

				// Follow BackEdge path backward to see if this is still a connected to its origin: it might be orphaned earlier in path
				int dist = 0;
				bool validPath = false;
				for (int curEdge = edge, curVertex = edges[curEdge].endVertex; !m_vertexInfos[curVertex].orphaned; curVertex = edges[curEdge].endVertex)
				{
					auto& curVertInfo = m_vertexInfos[curVertex];
					if (curVertInfo.stamp == m_stamp)
					{
						// We found this to be a valid path to origin already (see "mark edges" section below)
						dist += curVertInfo.dist;
						validPath = true;
						break;
					}

					++dist;
					curEdge = curVertInfo.backEdge;
					if (curEdge == terminalEdge)
					{
						curVertInfo.stamp = m_stamp;
						curVertInfo.dist = 1;
						validPath = true;
						break;
					}
					vxy_assert(m_vertexInfos[edges[curEdge].endVertex].fromSource == curVertInfo.fromSource);
				}

				if (validPath)
				{
					if (dist < minDistance)
					{
						minDistance = dist;
						bestEdge = edge;
					}

					// Mark edges to speed up other orphan checks that share a subset of the path
					for (int curVertex = edges[edge].endVertex; m_vertexInfos[curVertex].stamp != m_stamp; curVertex = edges[m_vertexInfos[curVertex].backEdge].endVertex)
					{
						m_vertexInfos[curVertex].stamp = m_stamp;
						m_vertexInfos[curVertex].dist = dist;
						--dist;
					}
				}
			}
		}

		if (bestEdge >= 0)
		{
			// Found a viable neighbor; relink the orphan.
			m_vertexInfos[orphan].orphaned = false;

			m_vertexInfos[orphan].backEdge = bestEdge;
			m_vertexInfos[orphan].stamp = m_stamp;
			m_vertexInfos[orphan].dist = minDistance + 1;
		}
		else
		{
			// No neighbors with capacity left, so this vertex is now inactive.
			// For each neighbor, if there is still capacity, add that neighbor to the active list.
			// If the parent edge of the neighbor points to us, then add it to orphan list.
			for (int edge = get<0>(map[orphan]), end = get<1>(map[orphan]); edge < end; ++edge)
			{
				const int nextVertex = edges[edge].endVertex;
				auto& nextVertInfo = m_vertexInfos[nextVertex];
				if (nextVertInfo.backEdge < 0 || nextVertInfo.fromSource != orphanFromSource)
				{
					continue;
				}
				const int revEdge = edges[edge].reverseEdgeIndex;
				const int flowEdge = orphanFromSource ? revEdge : edge;

				if (m_edgeCapacities[flowEdge] > 0 && !nextVertInfo.active)
				{
					nextVertInfo.active = true;
					m_vertexQueue.push_back(nextVertex);
				}
				if (nextVertInfo.backEdge != terminalEdge && !nextVertInfo.orphaned && edges[nextVertInfo.backEdge].endVertex == orphan)
				{
					nextVertInfo.backEdge = -1;
					nextVertInfo.orphaned = true;
					m_orphanQueue.push_back(nextVertex);
				}
			}

			vxy_assert(m_vertexInfos[orphan].backEdge < 0);
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
		vxy_assert(!m_vertexInfos[m_sink].active);
		m_vertexInfos[m_sink].active = true;
		m_vertexQueue.push_back(m_sink);

		while (!m_vertexQueue.empty())
		{
			int curVertex = m_vertexQueue.front();
			m_vertexQueue.pop_front();

			vxy_assert(m_vertexInfos[curVertex].active);

			for (int edge = get<0>(edgeMap[curVertex]); edge < get<1>(edgeMap[curVertex]); ++edge)
			{
				const int nextVertex = edges[edge].endVertex;
				if (!m_vertexInfos[nextVertex].active)
				{
					m_vertexInfos[nextVertex].fromSource = false;
					m_vertexInfos[nextVertex].backEdge = terminalEdge;

					if (m_edgeCapacities[edges[edge].reverseEdgeIndex] > 0)
					{
						m_vertexInfos[nextVertex].active = true;
						m_vertexQueue.push_back(nextVertex);
					}
					else
					{
						cutEdgeCallback(nextVertex, curVertex);
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

	mutable vector<MinCutVertexInfo> m_vertexInfos;
	mutable deque<int> m_vertexQueue;
	mutable bool m_computedCut = false;
};

} // namespace Vertexy