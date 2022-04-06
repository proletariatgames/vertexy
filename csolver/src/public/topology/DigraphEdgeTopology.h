// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once

#include <EASTL/hash_map.h>

#include "topology/ITopology.h"
#include "topology/DigraphTopology.h"

namespace csolver
{

// Node storage class for FEdgeTopology. Stores linkage to the original graph.
struct DigraphEdgeNode : public DigraphNode
{
	DigraphEdgeNode()
		: sourceFrom(-1)
		, sourceTo(-1)
		, bidirectional(false)
	{
	}

	DigraphEdgeNode(int sourceFrom, int sourceTo, bool bidirectional)
		: sourceFrom(sourceFrom)
		, sourceTo(sourceTo)
		, bidirectional(bidirectional)
	{
	}

	int sourceFrom;
	int sourceTo;
	bool bidirectional;
};

/**
 *  Digraph formed by converting all the edges in a source graph into nodes. Bidirectional edges
 *  in the source graph are converted into a single node.
 *
 *  Creating an TEdgeTopology out of a topology allows you to assign values to edges in the source topology,
 *  and quickly translate an edge in the source graph to a node in the edge graph.
 *
 *  NOTE does not (currently) respond to edge additions/deletions in the source graph.
 */
class EdgeTopology : public TDigraphTopologyBase<EdgeTopology, DigraphEdgeNode>
{
public:
	EdgeTopology(const shared_ptr<ITopology>& source, bool mergeBidirectional = true, bool connected = true)
		: m_source(source)
	{
		initialize(mergeBidirectional, connected);
	}

	EdgeTopology(const EdgeTopology& rhs) = delete;
	EdgeTopology(EdgeTopology&& rhs) = delete;

	// Given an edge from the source graph, get the corresponding node in our graph
	int getNodeForSourceEdge(int sourceFrom, int sourceTo) const
	{
		const int numOutgoing = m_source->getNumOutgoing(sourceFrom);
		for (int edgeIndex = 0; edgeIndex < numOutgoing; ++edgeIndex)
		{
			int destNode;
			if (m_source->getOutgoingDestination(sourceFrom, edgeIndex, destNode) && destNode == sourceTo)
			{
				return getNodeForSourceEdgeIndex(sourceFrom, edgeIndex);
			}
		}
		return -1;
	}

	// Given a node in our graph, return the corresponding edge in the source graph
	inline void getSourceEdgeForNode(int nodeIndex, int& sourceFrom, int& sourceTo, bool& bidirectional) const
	{
		auto& node = m_nodes[nodeIndex];
		sourceFrom = node.sourceFrom;
		sourceTo = node.sourceTo;
		bidirectional = node.bidirectional;
	}

	wstring nodeIndexToString(int nodeIndex) const
	{
		int sourceFrom, sourceTo;
		bool bidirectional;
		getSourceEdgeForNode(nodeIndex, sourceFrom, sourceTo, bidirectional);

		wstring out;
		out.sprintf(TEXT("%d%s%d"), sourceFrom, bidirectional ? TEXT("<->") : TEXT("->"), sourceTo);
		return out;
	}

	wstring edgeIndexToString(int edgeIndex) const
	{
		return {wstring::CtorSprintf(), TEXT("%d"), edgeIndex};
	}

	const shared_ptr<ITopology>& getSource() const { return m_source; }

protected:
	void initialize(bool mergeBidirectional, bool connected)
	{
		// Create a node for each edge in the source. Bidirectional edges share a single node.
		hash_map<tuple<int, int>, int> edgeMap;
		const int numNodes = m_source->getNumNodes();
		for (int nodeIdx = 0; nodeIdx < numNodes; ++nodeIdx)
		{
			const int numOutgoing = m_source->getNumOutgoing(nodeIdx);
			for (int edgeIndex = 0; edgeIndex < numOutgoing; ++edgeIndex)
			{
				int destNode;
				if (m_source->getOutgoingDestination(nodeIdx, edgeIndex, destNode))
				{
					cs_assert(destNode != nodeIdx);

					tuple<int, int> edgeDesc;
					bool bidirectional = false;
					if (mergeBidirectional && m_source->hasEdge(destNode, nodeIdx))
					{
						bidirectional = true;

						int minValue = nodeIdx < destNode ? nodeIdx : destNode;
						int maxValue = nodeIdx > destNode ? nodeIdx : destNode;
						edgeDesc = make_tuple(minValue, maxValue);
					}
					else
					{
						edgeDesc = make_tuple(nodeIdx, destNode);
					}

					if (edgeMap.find(edgeDesc) == edgeMap.end())
					{
						const int newNodeIdx = m_nodes.size();
						m_nodes.push_back(DigraphEdgeNode{nodeIdx, destNode, bidirectional});

						edgeMap[edgeDesc] = newNodeIdx;
					}
					m_sourceEdgeToNodeMap[make_tuple(nodeIdx, edgeIndex)] = edgeMap[edgeDesc];
				}
			}
		}

		if (connected)
		{
			// Create edges between the nodes. The node representing an edge in the source graph has an edge representing
			// every node that the source node is adjacent to.
			for (int nodeIndex = 0; nodeIndex < numNodes; ++nodeIndex)
			{
				const int numOutgoing = m_source->getNumOutgoing(nodeIndex);
				for (int edgeIndex1 = 0; edgeIndex1 < numOutgoing; ++edgeIndex1)
				{
					int destNode1;
					if (m_source->getOutgoingDestination(nodeIndex, edgeIndex1, destNode1))
					{
						for (int edgeIndex2 = 0; edgeIndex2 < numOutgoing; ++edgeIndex2)
						{
							if (edgeIndex1 == edgeIndex2)
							{
								continue;
							}

							int destNode2;
							if (m_source->getOutgoingDestination(nodeIndex, edgeIndex2, destNode2))
							{
								addEdge(getNodeForSourceEdgeIndex(nodeIndex, edgeIndex1), getNodeForSourceEdgeIndex(nodeIndex, edgeIndex2));
							}
						}
					}
				}
			}
		}
	}

	inline int getNodeForSourceEdgeIndex(int sourceNodeIndex, int sourceEdgeIdx) const
	{
		return m_sourceEdgeToNodeMap.find(make_tuple(sourceNodeIndex, sourceEdgeIdx))->second;
	}

	hash_map<tuple<int, int>, int> m_sourceEdgeToNodeMap;
	const shared_ptr<ITopology> m_source;
};

} // namespace csolver