// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once

#include "topology/Topology.h"

namespace Vertexy
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

	bool isValidVertex(int vertexIndex) const
	{
		return m_source->isValidVertex(vertexIndex);
	}

	int getNumOutgoing(int vertex) const
	{
		return m_source->getNumIncoming(vertex);
	}

	int getNumIncoming(int vertex) const
	{
		return m_source->getNumOutgoing(vertex);
	}

	int getNumVertices() const
	{
		return m_source->getNumVertices();
	}

	bool hasEdge(int from, int to) const
	{
		return m_source->hasEdge(to, from);
	}

	bool getIncomingSource(int vertex, int edgeIndex, int& outVertex) const
	{
		return m_source->getOutgoingDestination(vertex, edgeIndex, outVertex);
	}

	bool getOutgoingDestination(int vertex, int edgeIndex, int& outVertex) const
	{
		return m_source->getIncomingSource(vertex, edgeIndex, outVertex);
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

} // namespace Vertexy