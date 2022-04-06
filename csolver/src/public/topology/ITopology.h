// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once
#include "util/EventDispatcher.h"
#include <EASTL/shared_ptr.h>

#include "Topology.h"

namespace csolver
{

struct TopologyLink;

/** Generic interface for all topologies. Allows navigation of the topology without knowing underlying implementation.
 *
 *  To create from a raw topology, e.g.:
 *  shared_ptr<ITopology> TopoItf = ITopology::Adapt(Topo);
 *
 *  NOTE you should **NOT** implement this interface yourself. Create a TTopology-derived class instead, and wrap
 *  it with Adapt() to get an ITopology.
 */
class ITopology : public TTopology<ITopology>
{
public:
	virtual ~ITopology()
	{
	}

	/** Get the MAXIMUM number of outgoing arcs from the specified node. Each entry may or may not be a valid/traversable arc. */
	virtual int getNumOutgoing(int node) const = 0;

	/** Get the MAXIMUM number of incoming  arcs from the specified node. Each entry may or may not be a valid/traversable arc. */
	virtual int getNumIncoming(int node) const = 0;

	/** Given a node and incoming edge index, get the node that connects to us.
	 *  Returns true if the edge is traversable, false otherwise.
	 *  If upon return OutNode < 0, then there is no node at that edge.
	 **/
	virtual bool getIncomingSource(int node, int edgeIndex, int& outNode) const = 0;

	/** Given a node and outgoing edge index, get the node that we connect to.
	 *  Returns true if the edge is traversable, false otherwise.
	 *  If upon return OutNode < 0, then there is no node at that edge.
	 **/
	virtual bool getOutgoingDestination(int node, int edgeIndex, int& outNode) const = 0;
	virtual bool getOutgoingDestination(int node, int edgeIndex, int numTimes, int& outNode) const = 0;

	/** Whether this is a valid node index */
	virtual bool isValidNode(int nodeIndex) const = 0;

	/** Create a TopologyLink representing the path between the two nodes. Returns false if no path exists.
	 *  NOTE that this will return a link even if the edges exist but are not traversable (as defined by the specific topology type)
	 */
	virtual bool getTopologyLink(int startIndex, int endIndex, TopologyLink& outLink) const = 0;
	virtual bool areTopologyLinksEquivalent(const TopologyLink& first, const TopologyLink& second) const = 0;

	/** Get the total number of nodes */
	virtual int getNumNodes() const = 0;

	/** Get a display-friendly string for the node */
	virtual wstring nodeIndexToString(int nodeIndex) const = 0;
	virtual wstring edgeIndexToString(int edgeIndex) const = 0;

	/** Return whether there is an edge connecting From -> To. */
	virtual bool hasEdge(int from, int to) const = 0;

	/** Get the multicast delegate that will broadcast each time an edge is added or removed. */
	virtual OnTopologyEdgeChangeDispatcher& getEdgeChangeListener() = 0;

	/** Create an ITopology from a raw topology */
	template <typename T>
	static shared_ptr<ITopology> adapt(const shared_ptr<T>& topology);

	/** Get the underlying implementation of the graph. Not type-safe! */
	template <typename T>
	const shared_ptr<T>& getImplementation() const;
};

/** Implementation of ITopology for a specific implementation */
template <typename Impl, typename Itf=ITopology>
class TTopologyAdapter : public Itf
{
public:
	TTopologyAdapter(const shared_ptr<Impl>& impl)
		: m_implementation(impl)
	{
	}

	virtual int getNumOutgoing(int node) const override { return m_implementation->getNumOutgoing(node); }
	virtual int getNumIncoming(int node) const override { return m_implementation->getNumIncoming(node); }
	virtual bool getIncomingSource(int node, int edgeIndex, int& outNode) const override { return m_implementation->getIncomingSource(node, edgeIndex, outNode); }
	virtual bool getOutgoingDestination(int node, int edgeIndex, int& outNode) const override { return m_implementation->getOutgoingDestination(node, edgeIndex, outNode); }
	virtual bool getOutgoingDestination(int node, int edgeIndex, int numTimes, int& outNode) const override { return m_implementation->getOutgoingDestination(node, edgeIndex, numTimes, outNode); }
	virtual bool isValidNode(int nodeIndex) const override { return m_implementation->isValidNode(nodeIndex); }
	virtual bool getTopologyLink(int startIndex, int endIndex, TopologyLink& outLink) const override { return m_implementation->getTopologyLink(startIndex, endIndex, outLink); }
	virtual bool areTopologyLinksEquivalent(const TopologyLink& first, const TopologyLink& second) const override { return m_implementation->areTopologyLinksEquivalent(first, second); }
	virtual int getNumNodes() const override { return m_implementation->getNumNodes(); }
	virtual wstring nodeIndexToString(int nodeIndex) const override { return m_implementation->nodeIndexToString(nodeIndex); }
	virtual wstring edgeIndexToString(int edgeIndex) const override { return m_implementation->edgeIndexToString(edgeIndex); }
	virtual bool hasEdge(int from, int to) const override { return m_implementation->hasEdge(from, to); }
	virtual OnTopologyEdgeChangeDispatcher& getEdgeChangeListener() override { return m_implementation->getEdgeChangeListener(); }

	const shared_ptr<Impl>& getImplementation() const { return m_implementation; }

protected:
	shared_ptr<Impl> m_implementation;
};

template <typename T>
shared_ptr<ITopology> ITopology::adapt(const shared_ptr<T>& topology)
{
	if (topology->m_itf.get() == nullptr)
	{
		topology->m_itf = make_shared<typename T::AdapterType>(topology);
	}
	return topology->m_itf;
}

template <typename T>
const shared_ptr<T>& ITopology::getImplementation() const
{
	const TTopologyAdapter<T>* thiss = static_cast<const TTopologyAdapter<T>*>(this);
	return thiss->getImplementation();
}

} // namespace csolver