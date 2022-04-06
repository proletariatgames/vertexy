// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once

#include "topology/Topology.h"

namespace csolver
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

	bool isValidNode(int nodeIndex) const
	{
		return m_source->isValidNode(nodeIndex);
	}

	int getNumOutgoing(int node) const
	{
		return m_source->getNumIncoming(node) + m_source->getNumOutgoing(node);
	}

	int getNumIncoming(int node) const
	{
		return m_source->getNumOutgoing(node) + m_source->getNumIncoming(node);
	}

	int getNumNodes() const
	{
		return m_source->getNumNodes();
	}

	bool hasEdge(int from, int to) const
	{
		return m_source->hasEdge(to, from) || m_source->hasEdge(from, to);
	}

	bool getIncomingSource(int node, int edgeIndex, int& outNode) const
	{
		const int numIncoming = m_source->getNumIncoming(node);
		if (edgeIndex < numIncoming)
		{
			return m_source->getIncomingSource(node, edgeIndex, outNode);
		}
		else
		{
			return m_source->getOutgoingDestination(node, edgeIndex - numIncoming, outNode);
		}
	}

	bool getOutgoingDestination(int node, int edgeIndex, int& outNode) const
	{
		const int numOutgoing = m_source->getNumOutgoing(node);
		if (edgeIndex < numOutgoing)
		{
			return m_source->getOutgoingDestination(node, edgeIndex, outNode);
		}
		else
		{
			return m_source->getIncomingSource(node, edgeIndex - numOutgoing, outNode);
		}
	}

	/** Given a node and edge index, returns the index of residual edge outgoing from other side.
	 *  On return, OutNode is the node on other side.
	 */
	int getResidualForOutgoingEdge(int node, int edgeIndex, int& otherSide) const
	{
		bool success = getOutgoingDestination(node, edgeIndex, otherSide);
		cs_assert(success && otherSide >= 0);

		const int numOutgoing = m_source->getNumOutgoing(node);
		if (edgeIndex < numOutgoing)
		{
			// This is an original edge. We need the synthesized edge index on the other side.
			const int otherNumTotal = getNumOutgoing(otherSide);
			const int sourceNumOutgoing = m_source->getNumOutgoing(otherSide);
			for (int i = sourceNumOutgoing; i < otherNumTotal; ++i)
			{
				int dest;
				if (getOutgoingDestination(otherSide, i, dest) && dest == node)
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
				if (m_source->getOutgoingDestination(otherSide, i, dest) && dest == node)
				{
					return i;
				}
			}
		}

		cs_fail();
		otherSide = -1;
		return -1;
	}

	// Not currently supported:
	//bool GetTopologyLink(int StartIndex, int EndIndex, FTopologyLink& OutLink) const

	protected:
	const shared_ptr<T>& m_source;
};

} // namespace csolver