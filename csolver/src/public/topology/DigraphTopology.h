// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once

#include "ConstraintTypes.h"
#include "algo/ShortestPath.h"
#include "topology/Topology.h"

namespace csolver
{

/** Minimal implementation of a node in a directed graph */
struct DigraphNode
{
	// Edges coming out of this node, pointing to index of destination node
	vector<int> outEdges;
	// Edges coming into this node, pointing to index of source node
	vector<int> inEdges;

	void reset()
	{
		outEdges.clear();
		inEdges.clear();
	}
};

/** Implementation of topology for simple directed graphs */
template <typename Impl, typename NodeType=DigraphNode>
class TDigraphTopologyBase : public TTopology<Impl>
{
public:
	TDigraphTopologyBase()
	{
	}

	inline bool isValidNode(int nodeIndex) const { return nodeIndex >= 0 && nodeIndex < m_nodes.size(); }
	inline int getNumOutgoing(int node) const { return m_nodes[node].outEdges.size(); }
	inline int getNumIncoming(int node) const { return m_nodes[node].inEdges.size(); }
	inline int getNumNodes() const { return m_nodes.size(); }

	inline bool hasEdge(int from, int to) const
	{
		auto& edges = m_nodes[from].outEdges;
		return contains(edges.begin(), edges.end(), to);
	}

	bool getOutgoingDestination(int nodeIndex, int edgeIndex, int& outNode) const
	{
		auto& node = m_nodes[nodeIndex];
		if (edgeIndex >= node.outEdges.size())
		{
			return false;
		}
		outNode = node.outEdges[edgeIndex];
		return true;
	}

	bool getOutgoingDestination(int nodeIndex, int edgeIndex, int numTimes, int& outNode) const
	{
		outNode = nodeIndex;
		for (int i = 0; i < numTimes; ++i)
		{
			int nextNode;
			if (!getOutgoingDestination(outNode, edgeIndex, nextNode))
			{
				return false;
			}
			outNode = nextNode;
		}
		return true;
	}

	bool getIncomingSource(int nodeIndex, int edgeIndex, int& outNode) const
	{
		auto& node = m_nodes[nodeIndex];
		if (edgeIndex >= node.inEdges.size())
		{
			return false;
		}
		outNode = node.inEdges[edgeIndex];
		return true;
	}

	bool areTopologyLinksEquivalent(const TopologyLink& first, const TopologyLink& second) const
	{
		return first == second;
	}

	bool getTopologyLink(int startIndex, int endIndex, TopologyLink& outLink) const
	{
		ShortestPathAlgorithm shortestPathAlgorithm;

		vector<tuple<int, int>> path;
		if (!shortestPathAlgorithm.find(*this, startIndex, endIndex, path))
		{
			return false;
		}

		outLink.clear();
		for (int i = 0; i < path.size() - 1; ++i)
		{
			outLink.append(get<1>(path[i]), 1);
		}

		#ifdef CS_SANITY_CHECKS
		{
			int checkDest;
			cs_verify(outLink.resolve(*this, startIndex, checkDest));
			cs_sanity(checkDest == endIndex);
		}
		#endif

		return true;
	}

	wstring nodeIndexToString(int nodeIndex) const { return {wstring::CtorSprintf(), TEXT("%d"), nodeIndex}; }

	int addNode()
	{
		m_nodes.push_back({});
		return m_nodes.size() - 1;
	}

	void reset(int numNodes)
	{
		m_nodes.resize(numNodes);
		for (int i = 0; i < numNodes; ++i)
		{
			m_nodes[i].clear();
		}
	}

	void addEdge(int nodeFrom, int nodeTo)
	{
		if (!contains(m_nodes[nodeFrom].outEdges.begin(), m_nodes[nodeFrom].outEdges.end(), nodeTo))
		{
			m_nodes[nodeFrom].outEdges.push_back(nodeTo);
		}
		if (!contains(m_nodes[nodeTo].inEdges.begin(), m_nodes[nodeTo].inEdges.end(), nodeFrom))
		{
			m_nodes[nodeTo].inEdges.push_back(nodeFrom);
		}
		m_onEdgeChange.broadcast(true, nodeFrom, nodeTo);
	}

	void removeEdge(int nodeFrom, int nodeTo)
	{
		int edgeIdx = indexOf(m_nodes[nodeFrom].outEdges.begin(), m_nodes[nodeFrom].outEdges.end(), nodeTo);
		if (edgeIdx >= 0)
		{
			m_nodes[nodeFrom].outEdges.erase_unsorted(&m_nodes[nodeFrom].outEdges[edgeIdx]);
			m_nodes[nodeTo].inEdges.erase_first_unsorted(nodeFrom);
			m_onEdgeChange.broadcast(false, nodeFrom, nodeTo);
		}
	}

	OnTopologyEdgeChangeDispatcher& getEdgeChangeListener() { return m_onEdgeChange; }

protected:
	vector<NodeType> m_nodes;
	OnTopologyEdgeChangeDispatcher m_onEdgeChange;
};

/** Instantiation of directed graph topology */
class DigraphTopology : public TDigraphTopologyBase<DigraphTopology, DigraphNode>
{
public:
	DigraphTopology()
	{
	}
};

} // namespace csolver