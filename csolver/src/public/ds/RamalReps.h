// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once

#include <EASTL/hash_map.h>

#include "ConstraintTypes.h"
#include "FastLookupSet.h"
#include "topology/DigraphTopology.h"

namespace csolver
{
// For each node, for each outgoing edge, a tuple (EndNode, EdgeIndex)
using RamalRepsEdgeDefinitions = vector<vector<tuple<int, int>>>;

/** Implementation of Ramalingam & Reps dynamic algorithm for shortest path/reachability determination.
 *  See http://www.ccpo.odu.edu/~klinck/Reprints/PDF/ramalingamJAlgo1996.pdf
 *
 *  Note this implementation currently only supports graphs with unweighted edges.
 */
template <typename T=DigraphTopology, bool bPredefinedEdges = true>
class RamalReps
{
	template <bool bPredefined>
	struct TOutgoingEdgeIterator
	{
		inline TOutgoingEdgeIterator(const RamalReps& parent, int node)
			: m_parent(parent)
			, m_node(node)
			, m_edgeIdx(0)
		{
			if constexpr (bPredefined)
			{
				m_edgeDefs = &(*parent.m_edgeDefinitions)[node];
				m_endIdx = m_edgeDefs->size();
			}
			else
			{
				m_endIdx = parent.m_topo->getNumOutgoing(node);
			}
			scan();
		}

		inline operator bool() const
		{
			return m_edgeIdx < m_endIdx;
		}

		inline TOutgoingEdgeIterator& operator++()
		{
			++m_edgeIdx;
			scan();
			return *this;
		}

		inline int getEdgeId() const
		{
			if constexpr (bPredefined)
			{
				return get<1>((*m_edgeDefs)[m_edgeIdx]);
			}
			else
			{
				return m_parent.getEdgeId(m_node, m_dest);
			}
		}

		inline int getDestination() const
		{
			if constexpr (bPredefined)
			{
				return get<0>((*m_edgeDefs)[m_edgeIdx]);
			}
			else
			{
				return m_dest;
			}
		}

	protected:
		inline void scan()
		{
			if constexpr (bPredefined)
			{
				while (m_edgeIdx < m_endIdx && !m_parent.m_edgeEnabled[get<1>((*m_edgeDefs)[m_edgeIdx])])
				{
					++m_edgeIdx;
				}
			}
			else
			{
				while (m_edgeIdx < m_endIdx && !m_parent.m_topo->getOutgoingDestination(m_node, m_edgeIdx, m_dest))
				{
					++m_edgeIdx;
				}
			}
		}

		const RamalReps& m_parent;
		const vector<tuple<int, int>>* m_edgeDefs;
		int m_node;
		int m_edgeIdx;
		int m_endIdx;
		int m_dest;
	};

	using OutgoingEdgeIterator = TOutgoingEdgeIterator<bPredefinedEdges>;

public:
	using ReachabilityChangedDispatcher = TEventDispatcher<void(int /*Node*/, bool /*Reachable*/)>;
	using DistanceChangedDispatcher = TEventDispatcher<void(int /*Node*/, int /*DistanceFromSource*/)>;

	// Called whenever a node becomes (un)reachable from the source node.
	ReachabilityChangedDispatcher onReachabilityChanged;
	// Called whenever a node's shortest distance to the source node changes.
	DistanceChangedDispatcher onDistanceChanged;

	RamalReps(const shared_ptr<TTopology<T>>& topology, bool batchChanges = true, bool reportReachability = true, bool reportDistance = true)
		: m_topo(topology)
		, m_batchChanges(batchChanges)
		, m_reportReachability(reportReachability)
		, m_reportDistance(reportDistance)
	{
	}

	~RamalReps()
	{
		m_topo->getEdgeChangeListener().remove(m_edgeChangeListener);
	}

	void initialize(int inSourceNode, const RamalRepsEdgeDefinitions* inEdgeDefs = nullptr, int numEdges = -1)
	{
		m_sourceNode = inSourceNode;
		if constexpr (bPredefinedEdges)
		{
			cs_assert_msg(inEdgeDefs, "Must provide a predefined edge list for FRamalReps<T, true>");
			m_edgeDefinitions = inEdgeDefs;
		}

		m_edgeChangeListener = m_topo->getEdgeChangeListener().add([&](bool wasAdded, int from, int to)
		{
			if (m_batchChanges)
			{
				tuple<int, int> edge(from, to);
				if (wasAdded)
				{
					m_batchedEdgesRemoved.erase_first_unsorted(edge);
					m_batchedEdgesAdded.push_back(edge);
				}
				else
				{
					m_batchedEdgesAdded.erase_first_unsorted(edge);
					m_batchedEdgesRemoved.push_back(edge);
				}
			}
			else
			{
				if (wasAdded)
				{
					addEdge(from, to);
				}
				else
				{
					removeEdge(from, to);
				}

				processChanges();
			}
		});

		m_nodeDists.resize(m_topo->getNumNodes(), INT_MAX);
		m_nodeDists[m_sourceNode] = 0;

		m_lastNodeDists.resize(m_topo->getNumNodes(), INT_MAX);
		m_lastNodeDists[m_sourceNode] = 0;

		m_numShortestPathSources.resize(m_topo->getNumNodes(), 0);

		if constexpr (bPredefinedEdges)
		{
			m_edgeInShortestPath.reserve(numEdges);
			m_edgeEnabled.resize(numEdges, false);
		}
		else
		{
			const int guessedEdges = m_topo->getNumNodes() * 4; // guess, resize later if necessary
			m_edgeInShortestPath.reserve(guessedEdges);
			m_edgeToID.reserve(guessedEdges);
		}

		m_maybeShorterQueue.setIndexSize(m_topo->getNumNodes());
		m_maybeLongerQueue.setIndexSize(m_topo->getNumNodes());
		m_invalidationQueue.setIndexSize(m_topo->getNumNodes());
		m_workingQueue.setIndexSize(m_topo->getNumNodes());
		m_changedQueue.setIndexSize(m_topo->getNumNodes());

		// Process initial graph
		for (int src = 0; src < m_topo->getNumNodes(); ++src)
		{
			for (int edgeIdx = 0; edgeIdx < m_topo->getNumOutgoing(src); ++edgeIdx)
			{
				if (int dest; m_topo->getOutgoingDestination(src, edgeIdx, dest))
				{
					m_batchedEdgesAdded.push_back(make_tuple(src, dest));
				}
			}
		}
		refresh();
	}

	// If bBatchChanges is true, this must be called manually in order to update the reachability analysis.
	void refresh()
	{
		if (m_batchedEdgesAdded.empty() && m_batchedEdgesRemoved.empty())
		{
			return;
		}

		cs_assert(m_changedQueue.empty());

		for (auto& edge : m_batchedEdgesAdded)
		{
			addEdge(get<0>(edge), get<1>(edge));
		}

		for (auto& edge : m_batchedEdgesRemoved)
		{
			removeEdge(get<0>(edge), get<1>(edge));
		}

		processChanges();

		m_batchedEdgesAdded.clear();
		m_batchedEdgesRemoved.clear();
	}

	inline bool isReachable(int node) const { return m_nodeDists[node] != INT_MAX; }

protected:
	void addEdge(int from, int to)
	{
		if constexpr (bPredefinedEdges)
		{
			m_edgeEnabled[getEdgeId(from, to)] = true;
		}

		int distFrom = m_nodeDists[from];
		if (distFrom == INT_MAX)
		{
			// Not connected to source
			return;
		}

		int distTo = m_nodeDists[to];

		int newDistTo = distFrom + 1;
		if (distTo < newDistTo)
		{
			// shorter path than this exists
			return;
		}

		//
		// Edge is part of shortest path to source.
		//
		if (markShortestPathEdge(getEdgeId(from, to), true))
		{
			m_numShortestPathSources[to]++;

			if (distTo == newDistTo)
			{
				return;
			}

			m_nodeDists[to] = newDistTo;
			m_maybeShorterQueue.add(to);

			// Connecting this edge created a new shortest path, meaning other edges flowing into "To" may no
			// longer be part of a shortest path
			for (int edgeIdx = 0; edgeIdx < m_topo->getNumIncoming(to); ++edgeIdx)
			{
				if (int source; m_topo->getIncomingSource(to, edgeIdx, source))
				{
					if (m_nodeDists[to] < m_nodeDists[source] + 1)
					{
						if (markShortestPathEdge(getEdgeId(source, to), false))
						{
							cs_assert(m_numShortestPathSources[to] > 0);
							m_numShortestPathSources[to]--;
						}
					}
				}
			}
		}
	}

	void removeEdge(int from, int to)
	{
		if constexpr (bPredefinedEdges)
		{
			m_edgeEnabled[getEdgeId(from, to)] = false;
		}

		if (markShortestPathEdge(getEdgeId(from, to), false))
		{
			cs_assert(m_numShortestPathSources[to] > 0);
			m_numShortestPathSources[to]--;

			// If we've run out of sources that put this node on the shortest path, mark it to be processed.
			if (m_numShortestPathSources[to] == 0)
			{
				m_invalidationQueue.add(to);
			}
		}
	}

	void processChanges()
	{
		cs_assert(m_changedQueue.empty());

		// Iterate over invalidated nodes, reducing the number of shortest path sources for nodes reaching it.
		// If this causes a node to have no more sources, add it to the queue and recurse.
		for (int i = 0; i < m_invalidationQueue.size(); ++i)
		{
			const int node = m_invalidationQueue[i];

			m_nodeDists[node] = INT_MAX;
			for (OutgoingEdgeIterator it(*this, node); it; ++it)
			{
				if (markShortestPathEdge(it.getEdgeId(), false))
				{
					int dest = it.getDestination();
					cs_assert(m_numShortestPathSources[dest] > 0);
					m_numShortestPathSources[dest]--;
					if (m_numShortestPathSources[dest] == 0)
					{
						m_invalidationQueue.add(dest);
					}
				}
			}
		}

		// Now iterate over the full list of invalidated nodes, and check their incoming edges to see if there is
		// a route to the source. For the shortest route found, mark the edge as being on the shortest path and add
		// the node to the MaybeLongerQueue.
		for (int node : m_invalidationQueue)
		{
			cs_assert(m_nodeDists[node] == INT_MAX);

			int closestSource = -1;
			for (int edgeIdx = 0; edgeIdx < m_topo->getNumIncoming(node); ++edgeIdx)
			{
				if (int source; m_topo->getIncomingSource(node, edgeIdx, source))
				{
					if (m_nodeDists[source] == INT_MAX)
					{
						continue;
					}

					int distFromSource = m_nodeDists[source] + 1;
					if (m_nodeDists[node] > distFromSource)
					{
						m_nodeDists[node] = distFromSource;
						closestSource = source;
					}
				}
			}

			if (closestSource >= 0)
			{
				if (markShortestPathEdge(getEdgeId(closestSource, node), true))
				{
					m_numShortestPathSources[node]++;
				}
				m_maybeLongerQueue.add(node);
			}

			if (m_reportDistance || m_reportReachability)
			{
				m_changedQueue.add(node);
			}
		}

		// Handle all nodes that may have a longer path to source, or no longer have a path to source, due to
		// removed edges.
		handleMaybeLonger();

		// Handle all nodes that may have a shorter path to the source, due to added edges.
		handleMaybeShorter();

		// Send out change notifications
		if (m_reportDistance || m_reportReachability)
		{
			for (int node : m_changedQueue)
			{
				int dist = m_nodeDists[node];
				int lastDist = m_lastNodeDists[node];
				if (m_reportDistance && dist != lastDist)
				{
					onDistanceChanged.broadcast(node, m_nodeDists[node]);
				}
				if (m_reportReachability && (dist == INT_MAX) != (lastDist == INT_MAX))
				{
					onReachabilityChanged.broadcast(node, dist != INT_MAX);
				}
				m_lastNodeDists[node] = dist;
			}
		}

		m_invalidationQueue.clear();
		m_changedQueue.clear();
	}

	// Handle nodes who may have increased distance to source, or become disconnected from source.
	void handleMaybeLonger()
	{
		cs_assert(m_workingQueue.empty());

		// Sort queue by distance from source
		m_maybeLongerQueue.sort([&](int nodeLeft, int nodeRight)
		{
			return m_nodeDists[nodeLeft] < m_nodeDists[nodeRight];
		});

		// Go through the queue, from closest to furthest from source.
		// The WorkingQueue is used for items added during iteration, which will be inherently sorted by distance.
		// Each time, we pick the node with the least distance between MaybeLongerQueue and WorkingQueue.
		int i = 0, j = 0;
		while (i < m_maybeLongerQueue.size() || j < m_workingQueue.size())
		{
			int node;
			if (j == m_workingQueue.size() ||
				(i < m_maybeLongerQueue.size() && m_nodeDists[m_maybeLongerQueue[i]] < m_nodeDists[m_workingQueue[j]]))
			{
				node = m_maybeLongerQueue[i++];
				if (m_workingQueue.contains(node))
				{
					continue;
				}
			}
			else
			{
				node = m_workingQueue[j++];
			}

			if (m_reportDistance || m_reportReachability)
			{
				m_changedQueue.add(node);
			}

			// Update outgoing edges of this node that don't have a shorter path to source than us, and add
			// them to the WorkingQueue if so.
			for (OutgoingEdgeIterator it(*this, node); it; ++it)
			{
				int dest = it.getDestination();
				if (m_nodeDists[node] == INT_MAX && markShortestPathEdge(it.getEdgeId(), false))
				{
					cs_assert(m_numShortestPathSources[dest] > 0);
					m_numShortestPathSources[dest]--;

					if (m_reportDistance || m_reportReachability)
					{
						m_changedQueue.add(dest);
					}

					continue;
				}

				int newDestDist = m_nodeDists[node] + 1;
				if (m_nodeDists[dest] > newDestDist)
				{
					m_nodeDists[dest] = newDestDist;
					m_workingQueue.add(dest);
				}
				else if (m_nodeDists[dest] == newDestDist && markShortestPathEdge(it.getEdgeId(), true))
				{
					m_numShortestPathSources[dest]++;
				}
			}

			// Look at incoming edges for this node. If any nodes flowing to us are on the shortest path, then mark
			// the edge flowing to us as on the shortest path. If any edges have a greater distance from the source,
			// then mark the edge flowing to us as NOT on the shortest path.
			for (int edgeIdx = 0; edgeIdx < m_topo->getNumIncoming(node); ++edgeIdx)
			{
				if (int source; m_topo->getIncomingSource(node, edgeIdx, source))
				{
					if (m_nodeDists[source] == INT_MAX)
					{
						continue;
					}
					int newDist = m_nodeDists[source] + 1;
					if (m_nodeDists[node] == newDist && markShortestPathEdge(getEdgeId(source, node), true))
					{
						m_numShortestPathSources[node]++;
					}
					else if (m_nodeDists[node] < newDist && markShortestPathEdge(getEdgeId(source, node), false))
					{
						cs_assert(m_numShortestPathSources[node] > 0);
						m_numShortestPathSources[node]--;
					}
				}
			}
		}

		m_maybeLongerQueue.clear();
		m_workingQueue.clear();
	}

	// Handle any nodes that have been marked as now being part of the shortest path.
	void handleMaybeShorter()
	{
		cs_assert(m_workingQueue.empty());

		m_maybeShorterQueue.sort([&](int nodeLeft, int nodeRight)
		{
			return m_nodeDists[nodeLeft] < m_nodeDists[nodeRight];
		});

		// Go through the queue, from closest to furthest from source.
		// The WorkingQueue is used for items added during iteration, which will be inherently sorted by distance.
		// Each time, we pick the node with the least distance between MaybeShorterQueue and WorkingQueue.
		int i = 0, j = 0;
		while (i < m_maybeShorterQueue.size() || j < m_workingQueue.size())
		{
			int node;
			if (j == m_workingQueue.size() ||
				(i < m_maybeShorterQueue.size() && m_nodeDists[m_maybeShorterQueue[i]] < m_nodeDists[m_workingQueue[j]]))
			{
				node = m_maybeShorterQueue[i++];
				if (m_workingQueue.contains(node))
				{
					continue;
				}
			}
			else
			{
				node = m_workingQueue[j++];
			}

			// Recalculating this in the loop below:
			m_numShortestPathSources[node] = 0;

			if (m_reportDistance || m_reportReachability)
			{
				m_changedQueue.add(node);
			}

			// Look at each incoming edge. If there is a source node that is part of the shortest path, and we
			// continue that path, mark the edge as part of shortest path.
			// Likewise, if there is a source node that is further away from the source than us, mark the edge
			// as not part of the shortest path.
			for (int edgeIdx = 0; edgeIdx < m_topo->getNumIncoming(node); ++edgeIdx)
			{
				if (int source; m_topo->getIncomingSource(node, edgeIdx, source))
				{
					if (m_nodeDists[source] == INT_MAX)
					{
						markShortestPathEdge(getEdgeId(source, node), false);
						continue;
					}

					int newDist = m_nodeDists[source] + 1;
					if (m_nodeDists[node] == newDist)
					{
						markShortestPathEdge(getEdgeId(source, node), true);
						m_numShortestPathSources[node]++;
					}
					else if (m_nodeDists[node] < newDist)
					{
						markShortestPathEdge(getEdgeId(source, node), false);
					}
				}
			}

			// Look at each outgoing edge. If we're closer to the source than the node's currently assigned distance,
			// update its distance and add it to the working queue.
			// Otherwise, mark any outgoing edges that are on the shortest path as such.
			for (OutgoingEdgeIterator it(*this, node); it; ++it)
			{
				int dest = it.getDestination();
				if (m_nodeDists[node] == INT_MAX)
				{
					if (markShortestPathEdge(it.getEdgeId(), false))
					{
						cs_assert(m_numShortestPathSources[dest] > 0);
						m_numShortestPathSources[dest]--;
					}
				}
				else
				{
					int newDist = m_nodeDists[node] + 1;
					if (m_nodeDists[dest] > newDist)
					{
						m_nodeDists[dest] = newDist;
						m_workingQueue.add(dest);
					}
					else
					{
						if (m_nodeDists[dest] == newDist && markShortestPathEdge(it.getEdgeId(), true))
						{
							m_numShortestPathSources[dest]++;
						}
					}
				}
			}
		}

		m_maybeShorterQueue.clear();
		m_workingQueue.clear();
	}

	inline int getEdgeId(int from, int to) const
	{
		if constexpr (bPredefinedEdges)
		{
			const vector<tuple<int, int>>& outEdges = (*m_edgeDefinitions)[from];
			for (const tuple<int, int>& entry : outEdges)
			{
				if (get<0>(entry) == to)
				{
					return get<1>(entry);
				}
			}
			cs_fail();
			return -1;
		}
		else
		{
			tuple<int, int> edge(from, to);
			auto found = m_edgeToID.find(edge);
			if (found == m_edgeToID.end())
			{
				found = m_edgeToID.insert(make_tuple(edge, m_nextEdgeId++)).first;
			}
			return found->second;
		}
	}

	inline bool isEdgeInShortestPath(int edgeId) const
	{
		return edgeId >= 0 && edgeId < m_edgeInShortestPath.size() ? m_edgeInShortestPath[edgeId] : false;
	}

	inline bool markShortestPathEdge(int edgeId, bool shortestPath)
	{
		if (edgeId >= m_edgeInShortestPath.size())
		{
			m_edgeInShortestPath.resize(edgeId + 1, false);
		}

		if (m_edgeInShortestPath[edgeId] != shortestPath)
		{
			m_edgeInShortestPath[edgeId] = shortestPath;
			return true;
		}
		return false;
	}

	shared_ptr<TTopology<T>> m_topo;
	const bool m_batchChanges;
	const bool m_reportReachability;
	const bool m_reportDistance;

	int m_sourceNode = -1;
	// NodeID -> shortest path distance to source
	vector<int> m_nodeDists;
	// NodeDists values last time we propagated changes
	vector<int> m_lastNodeDists;
	// NodeID -> Number of nodes adjacent to it that are part of shortest path to source
	vector<int> m_numShortestPathSources;
	// EdgeID -> Is it part of a shortest path to source node
	vector<bool> m_edgeInShortestPath;
	// only valid if bPredefinedEdges is true
	vector<bool> m_edgeEnabled;

	// Set of nodes that may have become disconnected from or have longer paths to the source, due to removed edges.
	TFastLookupSet<int, true> m_invalidationQueue;
	// Set of nodes that may have become connected to or have shorter paths to the source, due to added edges.
	TFastLookupSet<int, true> m_maybeShorterQueue;
	// Set of nodes that are still connected, but may have longer paths to the source, due to removed edges.
	TFastLookupSet<int, true> m_maybeLongerQueue;
	// Temp working for HandleMaybeShorter/HandleMaybeLonger
	TFastLookupSet<int, true> m_workingQueue;
	// List of nodes that have changed as part of processing
	TFastLookupSet<int, true> m_changedQueue;

	// if bPredefinedEdges is true, this stores the set of all possible edges
	const RamalRepsEdgeDefinitions* m_edgeDefinitions = nullptr;

	// If bPredefinedEdges is false, this is used to associated edges with IDs as they are created.
	mutable hash_map<tuple<int, int>, int> m_edgeToID;
	mutable int m_nextEdgeId = 0;

	vector<tuple<int, int>> m_batchedEdgesAdded;
	vector<tuple<int, int>> m_batchedEdgesRemoved;

	// Listens to edge additions/removals
	EventListenerHandle m_edgeChangeListener = INVALID_EVENT_LISTENER_HANDLE;
};

} // namespace csolver