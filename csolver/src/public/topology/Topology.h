// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once

#include "ConstraintTypes.h"
#include "util/EventDispatcher.h"

namespace csolver
{

class ITopology;
template <typename T, typename Itf>
class TTopologyAdapter;
struct TopologyLink;

using OnTopologyEdgeChangeDispatcher = TEventDispatcher<void(bool, int, int)>;

/**
 * Base class for all topologies. Parameterized by the class deriving from this one.
 * The "Impl" argument allows topologies to share a common interface without virtual function overhead.
 */
template <typename Impl>
class TTopology
{
	friend class ITopology;
	friend class IPlanarTopology;

public:
	using AdapterType = TTopologyAdapter<Impl, ITopology>;
	using ImplType = Impl;
	using NodeID = int;

	TTopology()
	{
	}

	TTopology(const TTopology& rhs) = delete;
	TTopology(TTopology&& rhs) = delete;

	virtual ~TTopology()
	{
	}

	////////////////////////////////////////////////////////////////////////////
	//
	// The following functions need to be implemented by the subclass:

	/** Whether this is a valid node index */
	inline bool isValidNode(NodeID node) const { return static_cast<const Impl*>(this)->isValidNode(node); }

	/** Get the MAXIMUM number of outgoing arcs from the specified node. Each entry may or may not be a valid/traversable arc. */
	inline int getNumOutgoing(NodeID node) const { return static_cast<const Impl*>(this)->getNumOutgoing(node); }

	/** Get the MAXIMUM number of incoming  arcs from the specified node. Each entry may or may not be a valid/traversable arc. */
	inline int getNumIncoming(NodeID node) const { return static_cast<const Impl*>(this)->getNumIncoming(node); }

	/** Get the total number of nodes */
	inline int getNumNodes() const { return static_cast<const Impl*>(this)->getNumNodes(); }

	/** Return whether there is an edge connecting From -> To. */
	inline bool hasEdge(NodeID from, NodeID to) const { return static_cast<const Impl*>(this)->hasEdge(from, to); }

	/** Given a node and incoming edge index, get the node that connects to us.
	 *  Returns true if the edge is traversable, false otherwise.
	 *  If upon return OutNode < 0, then there is no node at that edge.
	 **/
	inline bool getIncomingSource(NodeID node, int edgeIndex, NodeID& outNode) const { return static_cast<const Impl*>(this)->getIncomingSource(node, edgeIndex, outNode); }

	/** Given a node and outgoing edge index, get the node that we connect to.
	 *  Returns true if the edge is traversable, false otherwise.
	 *  If upon return OutNode < 0, then there is no node at that edge.
	 **/
	inline bool getOutgoingDestination(NodeID node, int edgeIndex, NodeID& outNode) const { return static_cast<const Impl*>(this)->getOutgoingDestination(node, edgeIndex, outNode); }
	inline bool getOutgoingDestination(NodeID node, int edgeIndex, int numTimes, NodeID& outNode) const { return static_cast<const Impl*>(this)->getOutgoingDestination(node, edgeIndex, numTimes, outNode); }

	/** Create a TopologyLink representing the path between the two nodes. Returns false if no path exists.
	 *  NOTE that this will return a link even if the edges exist but are not traversable (as defined by the specific topology type)
	 */
	inline bool getTopologyLink(NodeID startNode, NodeID endNode, TopologyLink& outLink) const { return static_cast<const Impl*>(this)->getTopologyLink(startNode, endNode, outLink); }
	inline bool areTopologyLinksEquivalent(const TopologyLink& first, const TopologyLink& second) const { return static_cast<const Impl*>(this)->areTopologyLinksEquivalent(first, second); }

	/** Get the multicast delegate that will broadcast each time an edge is added or removed. */
	inline OnTopologyEdgeChangeDispatcher& getEdgeChangeListener() { return static_cast<Impl*>(this)->getEdgeChangeListener(); }

	/** Get a display-friendly string for the node */
	inline wstring nodeIndexToString(NodeID node) const { return static_cast<Impl*>(this)->nodeIndexToString(node); }
	inline wstring edgeIndexToString(int edgeIndex) const { return static_cast<Impl*>(this)->edgeIndexToString(edgeIndex); }

	//////////////////////////////// End required implementation ////////////////////////////////

	struct NeighborIterator
	{
		NeighborIterator(const TTopology& parent, NodeID node, int index)
			: m_parent(parent)
			, m_node(node)
			, m_index(index)
		{
			scan();
		}

		void operator++()
		{
			++m_index;
			scan();
		}

		bool operator!=(const NeighborIterator& rhs) const
		{
			cs_sanity(m_node == rhs.m_node);
			cs_sanity(&m_parent == &rhs.m_parent);
			return m_index != rhs.m_index;
		}

		NodeID operator*() const { return m_nextVal; }

	private:
		void scan()
		{
			while (m_index < m_parent.getNumOutgoing(m_node))
			{
				int outIndex;
				if (m_parent.getOutgoingDestination(m_node, m_index, 1, outIndex))
				{
					m_nextVal = outIndex;
					break;
				}
				++m_index;
			}
		}

		const TTopology& m_parent;
		const NodeID m_node;
		NodeID m_nextVal;
		int m_index;
	};

	struct NeighborIteratorContainer
	{
		NeighborIteratorContainer(const TTopology& parent, NodeID node)
			: m_parent(parent)
			, m_node(node)
		{
		}

		auto begin() const { return NeighborIterator(m_parent, m_node, 0); }
		auto end() const { return NeighborIterator(m_parent, m_node, m_parent.getNumOutgoing(m_node)); }

	private:
		const TTopology& m_parent;
		NodeID m_node;
	};

	/**
	 * Returns a class that supports C++ ranged iteration over the valid neighbors of the given node.
	 * e.g. for (int Neighbor : GetNeighbors(MyNode)) { ... }
	 */
	inline NeighborIteratorContainer getNeighbors(NodeID node)
	{
		cs_assert(isValidNode(node));
		return NeighborIteratorContainer(*this, node);
	}

protected:
	shared_ptr<class ITopology> m_itf;
};

} // namespace csolver