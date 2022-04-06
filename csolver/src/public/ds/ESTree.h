// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once

#include "ConstraintTypes.h"
#include "util/EventDispatcher.h"
#include "topology/DigraphTopology.h"
#include "topology/TopologySearch.h"
#include "topology/algo/BreadthFirstSearch.h"

#include <EASTL/deque.h>

namespace csolver
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
	using NodeReachabilityChanged = TEventDispatcher<void(int, bool)>;
	NodeReachabilityChanged onReachabilityChanged;

	ESTree(const shared_ptr<TTopology<T>>& topology,
		float ratioNodesAffectedBeforeRebuild = DEFAULT_RATIO_BEFORE_REBUILD,
		uint8_t numRequeuesBeforeRebuild = DEFAULT_REQUEUE_LIMIT_BEFORE_REBUILD)
		: m_topo(topology)
		, m_affectedRatioBeforeRebuild(ratioNodesAffectedBeforeRebuild)
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

	inline bool isReachable(int node) const { return m_nodeToLevel[node] != INT_MAX; }

	void initialize(int inSourceNode)
	{
		m_sourceNode = inSourceNode;

		m_nodeToParent.clear();
		m_nodeToParent.resize(m_topo->getNumNodes());
		m_nodeToParent[m_sourceNode] = -1;

		m_nodeToLevel.clear();
		m_nodeToLevel.resize(m_topo->getNumNodes(), INT_MAX);

		m_bfs.search(*m_topo.get(), m_sourceNode, [&](int level, int node, int parent)
		{
			if (parent >= 0)
			{
				m_nodeToParent[node] = parent;
			}
			else
			{
				cs_assert(node == m_sourceNode);
			}

			m_nodeToLevel[node] = level;
			onReachabilityChanged.broadcast(node, true);
			return ETopologySearchResponse::Continue;
		});

		cs_assert(m_nodeToLevel[m_sourceNode] == 0);
	}

protected:
	void addEdge(int from, int to)
	{
		cs_assert_msg(m_sourceNode >= 0, "Not initialized!");
		cs_assert(from != to);

		// If this is a link to the source node, will not affect reachability
		if (to == m_sourceNode)
		{
			return;
		}

		// If we can't reach the tail, then will not affect reachability
		if (!isReachable(from))
		{
			return;
		}

		// If this edge isn't part of shortest path, then will not affect reachability
		if (m_nodeToLevel[to] <= m_nodeToLevel[from] + 1)
		{
			return;
		}

		// Otherwise, parent the node to the tail
		{
			const int prevLevel = m_nodeToLevel[to];
			m_nodeToLevel[to] = m_nodeToLevel[from] + 1;
			m_nodeToParent[to] = from;

			if (prevLevel == INT_MAX)
			{
				onReachabilityChanged.broadcast(to, true);
			}
		}

		// Breadth-first search from the head to find any new shortest paths
		m_bfs.search(*m_topo.get(), to, [&](int curNode, int parent)
		{
			if (curNode == to)
			{
				return ETopologySearchResponse::Continue;
			}

			if (!isReachable(curNode) || m_nodeToLevel[curNode] > m_nodeToLevel[parent] + 1)
			{
				m_nodeToParent[curNode] = parent;

				const int prevLevel = m_nodeToLevel[curNode];
				m_nodeToLevel[curNode] = m_nodeToLevel[parent] + 1;

				if (prevLevel == INT_MAX)
				{
					onReachabilityChanged.broadcast(curNode, true);
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
		cs_assert_msg(m_sourceNode >= 0, "Not initialized!");
		cs_assert(from != to);

		// If both nodes are on the same level, no need to do anything.
		if (m_nodeToLevel[from] == m_nodeToLevel[to])
		{
			return;
		}

		// If this is a link to the source node, will not affect reachability
		if (to == m_sourceNode)
		{
			return;
		}

		// If this is a link to a node already unreachable, will not affect reachability
		if (m_nodeToLevel[to] == INT_MAX)
		{
			return;
		}

		cs_assert(m_nodeToLevel[to] >= 1);
		cs_sanity(!m_topo->hasEdge(from, to));

		// If this link is not being used to connect the nodes on the tree, will not affect reachability
		if (m_nodeToParent[to] != from)
		{
			return;
		}

		// Remove the parent link, then repair the tree
		m_nodeToParent[to] = -1;

		m_queuedCounter.clear();
		m_queuedCounter.resize(m_topo->getNumNodes(), 0);

		m_queue.clear();
		m_queue.push_back(to);
		m_queuedCounter[to]++;

		int processLimit = int(m_topo->getNumNodes() * m_affectedRatioBeforeRebuild);

		int numProcessed = 0;
		while (!m_queue.empty())
		{
			++numProcessed;

			const int curNode = m_queue.front();
			m_queue.pop_front();

			const int prevLevel = m_nodeToLevel[curNode];
			const int prevParent = m_nodeToParent[curNode];

			// Look at incoming edges for this node to find a new parent.
			int newParent = prevParent;
			int bestParentLevel = prevParent >= 0 ? m_nodeToLevel[prevParent] : INT_MAX;
			cs_assert(bestParentLevel != prevLevel-1);

			const int numIncomingEdges = m_topo->getNumIncoming(curNode);
			for (int edgeIdx = 0; edgeIdx < numIncomingEdges; ++edgeIdx)
			{
				int graphParent;
				if (m_topo->getIncomingSource(curNode, edgeIdx, graphParent))
				{
					const int graphParentLevel = m_nodeToLevel[graphParent];
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

			if (newParent >= 0 && bestParentLevel < m_topo->getNumNodes() - 1)
			{
				m_nodeToParent[curNode] = newParent;
				m_nodeToLevel[curNode] = bestParentLevel + 1;
				cs_assert(m_nodeToLevel[curNode] >= prevLevel);
			}
			// Otherwise, no longer reachable
			else if (m_nodeToLevel[curNode] != INT_MAX)
			{
				m_nodeToLevel[curNode] = INT_MAX;
				m_nodeToParent[curNode] = -1;

				onReachabilityChanged.broadcast(curNode, false);
			}

			bool limitReached = false;

			// If we moved to a lower level, update data structures and add all children to queue
			if (m_nodeToLevel[curNode] != prevLevel)
			{
				const int numOutgoingEdges = m_topo->getNumOutgoing(curNode);
				for (int edgeIdx = 0; edgeIdx < numOutgoingEdges; ++edgeIdx)
				{
					int graphChild;
					if (m_topo->getOutgoingDestination(curNode, edgeIdx, graphChild))
					{
						if (m_nodeToParent[graphChild] == curNode)
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

		m_nodeToParent.clear();
		m_nodeToParent.resize(m_topo->getNumNodes());
		m_nodeToParent[m_sourceNode] = -1;

		vector<int> prevNodeToLevel = m_nodeToLevel;

		m_nodeToLevel.clear();
		m_nodeToLevel.resize(m_topo->getNumNodes(), INT_MAX);

		m_bfs.search(*m_topo.get(), m_sourceNode, [&](int level, int node, int parent)
		{
			if (parent >= 0)
			{
				m_nodeToParent[node] = parent;
			}
			else
			{
				cs_assert(node == m_sourceNode);
			}

			m_nodeToLevel[node] = level;
			if (level != INT_MAX && prevNodeToLevel[node] == INT_MAX)
			{
				onReachabilityChanged.broadcast(node, true);
			}

			return ETopologySearchResponse::Continue;
		});

		cs_assert(m_nodeToLevel[m_sourceNode] == 0);

		for (int i = 0; i < m_nodeToLevel.size(); ++i)
		{
			if (prevNodeToLevel[i] != INT_MAX && m_nodeToLevel[i] == INT_MAX)
			{
				onReachabilityChanged.broadcast(i, false);
			}
		}
	}

	shared_ptr<TTopology<T>> m_topo;

	float m_affectedRatioBeforeRebuild;
	int m_requeueLimit;

	deque<int> m_queue;
	// The node that is the top of the tree
	int m_sourceNode = -1;
	// For each node, the parent node in the tree, or -1 if no parent
	vector<int> m_nodeToParent;
	// For each node, the level it is a part of.
	vector<int> m_nodeToLevel;
	// For each node, how many times it has been put in the process queue
	vector<uint8_t> m_queuedCounter;
	// Listens to edge additions/removals
	EventListenerHandle m_edgeChangeListener;
	BreadthFirstSearchAlgorithm m_bfs;
};

} // namespace csolver