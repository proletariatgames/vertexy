// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once

#include "topology/Topology.h"

namespace csolver
{

// Wrapper for a topology to reverse all edges, i.e. incoming edges become outgoing and outgoing edges become incoming.
template <typename Impl>
class TReversedTopology : public TTopology<TReversedTopology<Impl>>
{
public:
	TReversedTopology(const shared_ptr<TTopology<Impl>>& source)
		: m_source(source)
	{
	}

	bool isValidNode(int nodeIndex) const
	{
		return m_source->isValidNode(nodeIndex);
	}

	int getNumOutgoing(int node) const
	{
		return m_source->getNumIncoming(node);
	}

	int getNumIncoming(int node) const
	{
		return m_source->getNumOutgoing(node);
	}

	int getNumNodes() const
	{
		return m_source->getNumNodes();
	}

	bool hasEdge(int from, int to) const
	{
		return m_source->hasEdge(to, from);
	}

	bool getIncomingSource(int node, int edgeIndex, int& outNode) const
	{
		return m_source->getOutgoingDestination(node, edgeIndex, outNode);
	}

	bool getOutgoingDestination(int node, int edgeIndex, int& outNode) const
	{
		return m_source->getIncomingSource(node, edgeIndex, outNode);
	}

	bool areTopologyLinksEquivalent(const TopologyLink& first, const TopologyLink& second) const
	{
		return m_source->areTopologyLinksEquivalent(first, second);
	}

	bool getTopologyLink(int startIndex, int endIndex, TopologyLink& outLink) const
	{
		return m_source->getTopologyLink(endIndex, startIndex, outLink);
	}

protected:
	const shared_ptr<TTopology<Impl>>& m_source;
};

} // namespace csolver