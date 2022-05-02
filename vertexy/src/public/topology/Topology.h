// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once

#include "ConstraintTypes.h"
#include "util/EventDispatcher.h"

namespace Vertexy
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
	using VertexID = int;

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

	/** Whether this is a valid vertex index */
	inline bool isValidVertex(VertexID vertex) const { return static_cast<const Impl*>(this)->isValidVertex(vertex); }

	/** Get the MAXIMUM number of outgoing arcs from the specified vertex. Each entry may or may not be a valid/traversable arc. */
	inline int getNumOutgoing(VertexID vertex) const { return static_cast<const Impl*>(this)->getNumOutgoing(vertex); }

	/** Get the MAXIMUM number of incoming  arcs from the specified vertex. Each entry may or may not be a valid/traversable arc. */
	inline int getNumIncoming(VertexID vertex) const { return static_cast<const Impl*>(this)->getNumIncoming(vertex); }

	/** Get the total number of vertices */
	inline int getNumVertices() const { return static_cast<const Impl*>(this)->getNumVertices(); }

	/** Return whether there is an edge connecting From -> To. */
	inline bool hasEdge(VertexID from, VertexID to) const { return static_cast<const Impl*>(this)->hasEdge(from, to); }

	/** Given a vertex and incoming edge index, get the vertex that connects to us.
	 *  Returns true if the edge is traversable, false otherwise.
	 *  If upon return outVertex < 0, then there is no vertex at that edge.
	 **/
	inline bool getIncomingSource(VertexID vertex, int edgeIndex, VertexID& outVertex) const { return static_cast<const Impl*>(this)->getIncomingSource(vertex, edgeIndex, outVertex); }

	/** Given a vertex and outgoing edge index, get the vertex that we connect to.
	 *  Returns true if the edge is traversable, false otherwise.
	 *  If upon return outVertex < 0, then there is no vertex at that edge.
	 **/
	inline bool getOutgoingDestination(VertexID vertex, int edgeIndex, VertexID& outVertex) const { return static_cast<const Impl*>(this)->getOutgoingDestination(vertex, edgeIndex, outVertex); }
	inline bool getOutgoingDestination(VertexID vertex, int edgeIndex, int numTimes, VertexID& outVertex) const { return static_cast<const Impl*>(this)->getOutgoingDestination(vertex, edgeIndex, numTimes, outVertex); }

	/** Create a TopologyLink representing the path between the two vertices. Returns false if no path exists.
	 *  NOTE that this will return a link even if the edges exist but are not traversable (as defined by the specific topology type)
	 */
	inline bool getTopologyLink(VertexID startVertex, VertexID endVertex, TopologyLink& outLink) const { return static_cast<const Impl*>(this)->getTopologyLink(startVertex, endVertex, outLink); }
	inline bool areTopologyLinksEquivalent(const TopologyLink& first, const TopologyLink& second) const { return static_cast<const Impl*>(this)->areTopologyLinksEquivalent(first, second); }

	/** Get the multicast delegate that will broadcast each time an edge is added or removed. */
	inline OnTopologyEdgeChangeDispatcher& getEdgeChangeListener() { return static_cast<Impl*>(this)->getEdgeChangeListener(); }

	/** Get a display-friendly string for the vertex */
	inline wstring vertexIndexToString(VertexID vertex) const { return static_cast<const Impl*>(this)->vertexIndexToString(vertex); }
	inline wstring edgeIndexToString(int edgeIndex) const { return static_cast<const Impl*>(this)->edgeIndexToString(edgeIndex); }

	//////////////////////////////// End required implementation ////////////////////////////////

	struct NeighborIterator
	{
		NeighborIterator(const TTopology& parent, VertexID vertex, int index)
			: m_parent(parent)
			, m_vertex(vertex)
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
			vxy_sanity(m_vertex == rhs.m_vertex);
			vxy_sanity(&m_parent == &rhs.m_parent);
			return m_index != rhs.m_index;
		}

		VertexID operator*() const { return m_nextVal; }

	private:
		void scan()
		{
			while (m_index < m_parent.getNumOutgoing(m_vertex))
			{
				int outIndex;
				if (m_parent.getOutgoingDestination(m_vertex, m_index, 1, outIndex))
				{
					m_nextVal = outIndex;
					break;
				}
				++m_index;
			}
		}

		const TTopology& m_parent;
		const VertexID m_vertex;
		VertexID m_nextVal;
		int m_index;
	};

	struct NeighborIteratorContainer
	{
		NeighborIteratorContainer(const TTopology& parent, VertexID vertex)
			: m_parent(parent)
			, m_vertex(vertex)
		{
		}

		auto begin() const { return NeighborIterator(m_parent, m_vertex, 0); }
		auto end() const { return NeighborIterator(m_parent, m_vertex, m_parent.getNumOutgoing(m_vertex)); }

	private:
		const TTopology& m_parent;
		VertexID m_vertex;
	};

	/**
	 * Returns a class that supports C++ ranged iteration over the valid neighbors of the given vertex.
	 * e.g. for (int neighborVertex : getNeighbors(myVertex)) { ... }
	 */
	inline NeighborIteratorContainer getNeighbors(VertexID vertex) const
	{
		vxy_assert(isValidVertex(vertex));
		return NeighborIteratorContainer(*this, vertex);
	}

protected:
	shared_ptr<class ITopology> m_itf;
};

} // namespace Vertexy