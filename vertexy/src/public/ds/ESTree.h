// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once

#include "ConstraintTypes.h"
#include "util/EventDispatcher.h"
#include "topology/DigraphTopology.h"
#include "topology/TopologySearch.h"
#include "topology/algo/BreadthFirstSearch.h"

#include <EASTL/deque.h>

namespace Vertexy
{

/** Implementation of Even-Shiloach trees. Allows for reachability determination from a single source with dynamic updates.
 *  See https://www.uni-trier.de/fileadmin/fb4/prof/INF/DEA/Uebungen_LVA-Ankuendigungen/ws07/KAuD/onl.pdf
 */
template <typename T=DigraphTopology>
class ESTree
{
	static constexpr bool USE_REBUILDS = true;
	static constexpr float DEFAULT_RATIO_BEFORE_REBUILD = 0.75f;
	static constexpr uint8_t DEFAULT_REQUEUE_LIMIT_BEFORE_REBUILD = 50;

public:
	using VertexReachabilityChanged = TEventDispatcher<void(int, bool)>;
	VertexReachabilityChanged onReachabilityChanged;

	ESTree(const shared_ptr<TTopology<T>>& topology,
		float ratioVerticesAffectedBeforeRebuild = DEFAULT_RATIO_BEFORE_REBUILD,
		uint8_t numRequeuesBeforeRebuild = DEFAULT_REQUEUE_LIMIT_BEFORE_REBUILD)
		: m_topo(topology)
		, m_affectedRatioBeforeRebuild(ratioVerticesAffectedBeforeRebuild)
		, m_requeueLimit(numRequeuesBeforeRebuild)
	{
		m_edgeChangeListener = m_topo->getEdgeChangeListener().add([&](bool wasAdded, int from, int to)
		{
			if (wasAdded)
			{
				addEdge(from, to);
			}
			else
			{
				removeEdge(from, to);
			}
		});
	}

	~ESTree()
	{
		m_topo->getEdgeChangeListener().remove(m_edgeChangeListener);
	}

	inline bool isReachable(int vertex) const { return m_vertexToLevel[vertex] != INT_MAX; }

	void initialize(int sourceVertex)
	{
		m_sourceVertex = sourceVertex;

		m_vertexToParent.clear();
		m_vertexToParent.resize(m_topo->getNumVertices());
		m_vertexToParent[m_sourceVertex] = -1;

		m_vertexToLevel.clear();
		m_vertexToLevel.resize(m_topo->getNumVertices(), INT_MAX);

		m_bfs.search(*m_topo.get(), m_sourceVertex, [&](int level, int vertex, int parent)
		{
			if (parent >= 0)
			{
				m_vertexToParent[vertex] = parent;
			}
			else
			{
				vxy_assert(vertex == m_sourceVertex);
			}

			m_vertexToLevel[vertex] = level;
			onReachabilityChanged.broadcast(vertex, true);
			return ETopologySearchResponse::Continue;
		});

		vxy_assert(m_vertexToLevel[m_sourceVertex] == 0);
	}

protected:
	void addEdge(int from, int to)
	{
		vxy_assert_msg(m_sourceVertex >= 0, "Not initialized!");
		vxy_assert(from != to);

		// If this is a link to the source vertex, will not affect reachability
		if (to == m_sourceVertex)
		{
			return;
		}

		// If we can't reach the tail, then will not affect reachability
		if (!isReachable(from))
		{
			return;
		}

		// If this edge isn't part of shortest path, then will not affect reachability
		if (m_vertexToLevel[to] <= m_vertexToLevel[from] + 1)
		{
			return;
		}

		// Otherwise, parent the vertex to the tail
		{
			const int prevLevel = m_vertexToLevel[to];
			m_vertexToLevel[to] = m_vertexToLevel[from] + 1;
			m_vertexToParent[to] = from;

			if (prevLevel == INT_MAX)
			{
				onReachabilityChanged.broadcast(to, true);
			}
		}

		// Breadth-first search from the head to find any new shortest paths
		m_bfs.search(*m_topo.get(), to, [&](int curVertex, int parent)
		{
			if (curVertex == to)
			{
				return ETopologySearchResponse::Continue;
			}

			if (!isReachable(curVertex) || m_vertexToLevel[curVertex] > m_vertexToLevel[parent] + 1)
			{
				m_vertexToParent[curVertex] = parent;

				const int prevLevel = m_vertexToLevel[curVertex];
				m_vertexToLevel[curVertex] = m_vertexToLevel[parent] + 1;

				if (prevLevel == INT_MAX)
				{
					onReachabilityChanged.broadcast(curVertex, true);
				}

				return ETopologySearchResponse::Continue;
			}
			else
			{
				// No need to continue down this branch; there is a shorter path
				return ETopologySearchResponse::Skip;
			}
		});
	}

	void removeEdge(int from, int to)
	{
		vxy_assert_msg(m_sourceVertex >= 0, "Not initialized!");
		vxy_assert(from != to);

		// If both vertices are on the same level, no need to do anything.
		if (m_vertexToLevel[from] == m_vertexToLevel[to])
		{
			return;
		}

		// If this is a link to the source vertex, will not affect reachability
		if (to == m_sourceVertex)
		{
			return;
		}

		// If this is a link to a vertex already unreachable, will not affect reachability
		if (m_vertexToLevel[to] == INT_MAX)
		{
			return;
		}

		vxy_assert(m_vertexToLevel[to] >= 1);
		vxy_sanity(!m_topo->hasEdge(from, to));

		// If this link is not being used to connect the vertices on the tree, will not affect reachability
		if (m_vertexToParent[to] != from)
		{
			return;
		}

		// Remove the parent link, then repair the tree
		m_vertexToParent[to] = -1;

		m_queuedCounter.clear();
		m_queuedCounter.resize(m_topo->getNumVertices(), 0);

		m_queue.clear();
		m_queue.push_back(to);
		m_queuedCounter[to]++;

		int processLimit = int(m_topo->getNumVertices() * m_affectedRatioBeforeRebuild);

		int numProcessed = 0;
		while (!m_queue.empty())
		{
			++numProcessed;

			const int curVertex = m_queue.front();
			m_queue.pop_front();

			const int prevLevel = m_vertexToLevel[curVertex];
			const int prevParent = m_vertexToParent[curVertex];

			// Look at incoming edges for this vertex to find a new parent.
			int newParent = prevParent;
			int bestParentLevel = prevParent >= 0 ? m_vertexToLevel[prevParent] : INT_MAX;
			vxy_assert(bestParentLevel != prevLevel-1);

			const int numIncomingEdges = m_topo->getNumIncoming(curVertex);
			for (int edgeIdx = 0; edgeIdx < numIncomingEdges; ++edgeIdx)
			{
				int graphParent;
				if (m_topo->getIncomingSource(curVertex, edgeIdx, graphParent))
				{
					const int graphParentLevel = m_vertexToLevel[graphParent];
					if (graphParentLevel < bestParentLevel)
					{
						bestParentLevel = graphParentLevel;
						newParent = graphParent;
						if (bestParentLevel == prevLevel - 1)
						{
							break;
						}
					}
				}
			}

			if (newParent >= 0 && bestParentLevel < m_topo->getNumVertices() - 1)
			{
				m_vertexToParent[curVertex] = newParent;
				m_vertexToLevel[curVertex] = bestParentLevel + 1;
				vxy_assert(m_vertexToLevel[curVertex] >= prevLevel);
			}
			// Otherwise, no longer reachable
			else if (m_vertexToLevel[curVertex] != INT_MAX)
			{
				m_vertexToLevel[curVertex] = INT_MAX;
				m_vertexToParent[curVertex] = -1;

				onReachabilityChanged.broadcast(curVertex, false);
			}

			bool limitReached = false;

			// If we moved to a lower level, update data structures and add all children to queue
			if (m_vertexToLevel[curVertex] != prevLevel)
			{
				const int numOutgoingEdges = m_topo->getNumOutgoing(curVertex);
				for (int edgeIdx = 0; edgeIdx < numOutgoingEdges; ++edgeIdx)
				{
					int graphChild;
					if (m_topo->getOutgoingDestination(curVertex, edgeIdx, graphChild))
					{
						if (m_vertexToParent[graphChild] == curVertex)
						{
							if constexpr (USE_REBUILDS)
							{
								m_queuedCounter[graphChild]++;
								if (m_queuedCounter[graphChild] >= m_requeueLimit)
								{
									limitReached = true;
									break;
								}
							}
							m_queue.push_back(graphChild);
						}
					}
				}
			}

			if (!m_queue.empty() && (numProcessed + m_queue.size()) > processLimit)
			{
				limitReached = true;
			}

			// Heuristic: if we are doing too much processing, give up and just rebuild from scratch.
			if (USE_REBUILDS && limitReached)
			{
				rebuild();
				break;
			}
		}
	}

	void rebuild()
	{
		m_queue.clear();

		m_vertexToParent.clear();
		m_vertexToParent.resize(m_topo->getNumVertices());
		m_vertexToParent[m_sourceVertex] = -1;

		vector<int> prevVertexToLevel = m_vertexToLevel;

		m_vertexToLevel.clear();
		m_vertexToLevel.resize(m_topo->getNumVertices(), INT_MAX);

		m_bfs.search(*m_topo.get(), m_sourceVertex, [&](int level, int vertex, int parent)
		{
			if (parent >= 0)
			{
				m_vertexToParent[vertex] = parent;
			}
			else
			{
				vxy_assert(vertex == m_sourceVertex);
			}

			m_vertexToLevel[vertex] = level;
			if (level != INT_MAX && prevVertexToLevel[vertex] == INT_MAX)
			{
				onReachabilityChanged.broadcast(vertex, true);
			}

			return ETopologySearchResponse::Continue;
		});

		vxy_assert(m_vertexToLevel[m_sourceVertex] == 0);

		for (int i = 0; i < m_vertexToLevel.size(); ++i)
		{
			if (prevVertexToLevel[i] != INT_MAX && m_vertexToLevel[i] == INT_MAX)
			{
				onReachabilityChanged.broadcast(i, false);
			}
		}
	}

	shared_ptr<TTopology<T>> m_topo;

	float m_affectedRatioBeforeRebuild;
	int m_requeueLimit;

	deque<int> m_queue;
	// The vertex that is the top of the tree
	int m_sourceVertex = -1;
	// For each vertex, the parent vertex in the tree, or -1 if no parent
	vector<int> m_vertexToParent;
	// For each vertex, the level it is a part of.
	vector<int> m_vertexToLevel;
	// For each vertex, how many times it has been put in the process queue
	vector<uint8_t> m_queuedCounter;
	// Listens to edge additions/removals
	EventListenerHandle m_edgeChangeListener;
	BreadthFirstSearchAlgorithm m_bfs;
};

} // namespace Vertexy