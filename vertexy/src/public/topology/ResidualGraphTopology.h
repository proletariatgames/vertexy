// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once

#include "topology/Topology.h"

namespace Vertexy
{

/**
 * Wrapper for a topology to create the residual graph: every edge of original graph is represented twice, both as
 * forward and reverse.
 */
template <typename T>
class TResidualGraphTopology : public TTopology<TResidualGraphTopology<T>>
{
public:
	TResidualGraphTopology(const shared_ptr<T>& source)
		: m_source(source)
	{
	}

	bool isValidVertex(int vertexIndex) const
	{
		return m_source->isValidVertex(vertexIndex);
	}

	int getNumOutgoing(int vertex) const
	{
		return m_source->getNumIncoming(vertex) + m_source->getNumOutgoing(vertex);
	}

	int getNumIncoming(int vertex) const
	{
		return m_source->getNumOutgoing(vertex) + m_source->getNumIncoming(vertex);
	}

	int getNumVertices() const
	{
		return m_source->getNumVertices();
	}

	bool hasEdge(int from, int to) const
	{
		return m_source->hasEdge(to, from) || m_source->hasEdge(from, to);
	}

	bool getIncomingSource(int vertex, int edgeIndex, int& outVertex) const
	{
		const int numIncoming = m_source->getNumIncoming(vertex);
		if (edgeIndex < numIncoming)
		{
			return m_source->getIncomingSource(vertex, edgeIndex, outVertex);
		}
		else
		{
			return m_source->getOutgoingDestination(vertex, edgeIndex - numIncoming, outVertex);
		}
	}

	bool getOutgoingDestination(int vertex, int edgeIndex, int& outVertex) const
	{
		const int numOutgoing = m_source->getNumOutgoing(vertex);
		if (edgeIndex < numOutgoing)
		{
			return m_source->getOutgoingDestination(vertex, edgeIndex, outVertex);
		}
		else
		{
			return m_source->getIncomingSource(vertex, edgeIndex - numOutgoing, outVertex);
		}
	}

	/** Given a vertex and edge index, returns the index of residual edge outgoing from other side.
	 *  On return, otherSide is the vertex on other side.
	 */
	int getResidualForOutgoingEdge(int vertex, int edgeIndex, int& otherSide) const
	{
		bool success = getOutgoingDestination(vertex, edgeIndex, otherSide);
		vxy_assert(success && otherSide >= 0);

		const int numOutgoing = m_source->getNumOutgoing(vertex);
		if (edgeIndex < numOutgoing)
		{
			// This is an original edge. We need the synthesized edge index on the other side.
			const int otherNumTotal = getNumOutgoing(otherSide);
			const int sourceNumOutgoing = m_source->getNumOutgoing(otherSide);
			for (int i = sourceNumOutgoing; i < otherNumTotal; ++i)
			{
				int dest;
				if (getOutgoingDestination(otherSide, i, dest) && dest == vertex)
				{
					return i;
				}
			}
		}
		else
		{
			// This is a synthesized edge. We need the original edge index from the other side.
			const int sourceNumOutgoing = m_source->getNumOutgoing(otherSide);
			for (int i = 0; i < sourceNumOutgoing; ++i)
			{
				int dest;
				if (m_source->getOutgoingDestination(otherSide, i, dest) && dest == vertex)
				{
					return i;
				}
			}
		}

		vxy_fail();
		otherSide = -1;
		return -1;
	}

	// Not currently supported:
	//bool GetTopologyLink(int StartIndex, int EndIndex, FTopologyLink& OutLink) const

	protected:
	const shared_ptr<T>& m_source;
};

} // namespace Vertexy