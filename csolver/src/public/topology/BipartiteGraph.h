// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once

#include "ConstraintTypes.h"
#include "topology/Topology.h"
#include "topology/TopologyLink.h"
#include <EASTL/bonus/ring_buffer.h>

namespace csolver
{

// Bipartite (two-sided) graph structure
// Essentially there are two sets of nodes: Left and Right, and edges connecting elements of Left to Right.
class BipartiteGraph : public TTopology<BipartiteGraph>
{
public:
	BipartiteGraph()
		: m_numLeft(0)
		, m_numRight(0)
	{
	}

	BipartiteGraph(int numLeftNodes, int numRightNodes, const vector<int>* inRightCapacities = nullptr)
	{
		initialize(numLeftNodes, numRightNodes, inRightCapacities);
	}

	//////////////////////////////////////
	// TTopology implementation

	bool isValidNode(int index) const { return index >= 0 && index < m_numLeft + m_numRight; }
	int getNumNodes() const { return m_numLeft + m_numRight; }
	OnTopologyEdgeChangeDispatcher& getEdgeChangeListener() { return m_onEdgeChange; }

	int getNumOutgoing(int node) const
	{
		return (node < m_numLeft) ? m_adjLeft[node].size() : 0;
	}

	int getNumIncoming(int node)
	{
		return (node >= m_numLeft) ? m_adjRight[node - m_numLeft].size() : 0;
	}

	bool hasEdge(int from, int to) const
	{
		return from < m_numLeft
				   ? contains(m_adjLeft[from].begin(), m_adjLeft[from].end(), to - m_numLeft)
				   : 0;
	}

	bool getIncomingSource(int node, int edgeIndex, int& outNode)
	{
		if (node >= m_numLeft)
		{
			outNode = m_adjRight[node - m_numLeft][edgeIndex];
			return true;
		}
		else
		{
			outNode = -1;
			return false;
		}
	}

	bool getOutgoingDestination(int node, int edgeIndex, int& outNode) const
	{
		if (node < m_numLeft)
		{
			outNode = m_adjLeft[node][edgeIndex];
			return true;
		}
		else
		{
			outNode = -1;
			return false;
		}
	}

	bool getTopologyLink(int startNode, int endNode, TopologyLink& outLink) const
	{
		// Can only move from left node to connected right node; that's it.
		if (startNode < m_numLeft)
		{
			int rightIdx = indexOf(m_adjLeft[startNode].begin(), m_adjLeft[startNode].end(), endNode - m_numLeft);
			if (rightIdx >= 0)
			{
				outLink.append(0, 1);
				return true;
			}
		}
		return false;
	}

	wstring nodeIndexToString(int nodeIndex) const
	{
		if (nodeIndex < m_numLeft)
		{
			return {wstring::CtorSprintf(), TEXT("Left-%d"), nodeIndex};
		}
		else
		{
			return {wstring::CtorSprintf(), TEXT("Right-%d"), nodeIndex - m_numLeft};
		}
	}

	wstring edgeIndexToString(int edgeIndex) const
	{
		return {wstring::CtorSprintf(), TEXT("%d"), edgeIndex};
	}

	// TTopology implementation end
	//////////////////////////////////////

	void initialize(int numLeftNodes, int numRightNodes, const vector<int>* inRightCapacities = nullptr)
	{
		m_numLeft = numLeftNodes;
		m_numRight = numRightNodes;
		clearAllEdges();

		m_adjLeft.resize(numLeftNodes);
		for (int i = 0; i < m_adjLeft.size(); ++i)
		{
			m_adjLeft[i].clear();
		}

		m_adjRight.resize(numRightNodes);
		for (int i = 0; i < m_adjRight.size(); ++i)
		{
			m_adjRight[i].clear();
		}

		m_pairLeft.clear();
		m_pairLeft.resize(m_numLeft, -1);

		m_rightCapacities.clear();
		if (inRightCapacities)
		{
			cs_assert(inRightCapacities->size() <= numRightNodes);
			m_rightCapacities.insert(m_rightCapacities.end(), inRightCapacities->begin(), inRightCapacities->end());
			while (m_rightCapacities.size() < m_numRight)
			{
				m_rightCapacities.push_back(0);
			}
		}

		m_matchedNumRight.clear();
		m_matchedNumRight.resize(numRightNodes, 0);
	}

	void reserveEdges(int leftNode, int num)
	{
		m_adjLeft[leftNode].reserve(num);
	}

	void addEdge(int leftNode, int rightNode)
	{
		cs_sanity(!contains( m_adjLeft[leftNode].begin(), m_adjLeft[leftNode].end(), rightNode));
		cs_sanity(!contains( m_adjRight[rightNode].begin(), m_adjRight[rightNode].end(), leftNode));
		m_adjLeft[leftNode].push_back(rightNode);
		m_adjRight[rightNode].push_back(leftNode);

		++m_numEdges;

		m_onEdgeChange.broadcast(true, leftNode, m_numLeft + rightNode);
	}

	bool hasBipartiteEdge(int leftNode, int rightNode) const
	{
		return contains(m_adjLeft[leftNode].begin(), m_adjLeft[leftNode].end(), rightNode);
	}

	// Removes all edges originating from LeftNode
	void removeEdges(int left)
	{
		const int leftNode = left;
		for (int i = 0; i < m_adjLeft[leftNode].size(); ++i)
		{
			int rightNode = m_adjLeft[leftNode][i];
			cs_assert(rightNode >= 0);

			m_adjLeft[leftNode][i] = 0;
			m_adjRight[rightNode].erase_first_unsorted(leftNode);
			m_onEdgeChange.broadcast(false, left, m_numLeft + rightNode);

			m_numEdges--;
		}

		match(leftNode, -1);
		cs_assert(m_pairLeft[leftNode] < 0);

		m_adjLeft[leftNode].clear();
	}

	void clearAllEdges()
	{
		if (m_onEdgeChange.isBound())
		{
			for (int i = 0; i < m_adjLeft.size(); ++i)
			{
				for (int j = 0; j < m_adjLeft[i].size(); ++j)
				{
					int right = m_adjLeft[i][j];
					cs_assert(right >= 0);

					m_adjLeft[i][j] = 0;
					m_adjRight[right].erase_first_unsorted(i);
					m_onEdgeChange.broadcast(false, i, m_numLeft + right);
				}
			}
		}

		for (int i = 0; i < m_adjLeft.size(); ++i)
		{
			m_adjLeft[i].clear();
		}
		for (int i = 0; i < m_adjRight.size(); ++i)
		{
			m_adjRight[i].clear();
		}

		m_pairLeft.clear();
		m_pairLeft.resize(m_numLeft, -1);

		m_matchedNumRight.clear();
		m_matchedNumRight.resize(m_numRight, 0);
		m_numEdges = 0;
	}

	inline int getMatchedRightSide(int left) const
	{
		return m_pairLeft[left];
	}

	inline int getNumRightSideMatched(int right) const
	{
		return m_matchedNumRight[right];
	}

	int getNumEdges() const { return m_numEdges; }

	// Non-incremental API.
	// @param LeftNodeToPrioritize If specified, the given left node will be matched after everything else is matched.
	void computeMaximalMatching(int leftNodeToDeprioritize = -1);

	// Incremental API.
	// Call RemoveEdges/AddEdge as necessary. Reuses the previous matching if one exists.
	// @return Whether a full matching has been found (i.e. all left nodes match with a right node)
	bool incrementalMaximalMatching(int leftNodeToDeprioritize = -1);

protected:
	int hopcroftBfs(int deprioritize, vector<int>& leftEdges);
	bool findAugmentingPath(int left);

	inline int getRightCapacity(int rightNode) const
	{
		return m_rightCapacities.empty() ? 1 : m_rightCapacities[rightNode];
	}

	inline bool isBelowCapacity(int rightNode) const
	{
		return m_matchedNumRight[rightNode] < getRightCapacity(rightNode);
	}

	inline void match(int left, int right)
	{
		int prevRight = getMatchedRightSide(left);
		if (prevRight >= 0)
		{
			cs_assert(m_matchedNumRight[prevRight] > 0);
			m_matchedNumRight[prevRight]--;
		}

		m_pairLeft[left] = right;
		if (right >= 0)
		{
			m_matchedNumRight[right]++;
			cs_assert(m_matchedNumRight[right] <= getRightCapacity(right));
		}
	}

	// Number of nodes on the left side of the graph
	int m_numLeft;
	// Number of nodes on the right side of the graph
	int m_numRight;

	// Edges from Left->Right
	vector<vector<int>> m_adjLeft;
	vector<vector<int>> m_adjRight;

	// Pairings from Left->Right
	vector<int> m_pairLeft;

	// Capacities for each right node
	vector<int> m_rightCapacities;
	// How many are matched on right side
	vector<int> m_matchedNumRight;

	// FIFO queue for BFS
	ring_buffer<int> m_queue;
	// For each node, the list of outgoing nodes discovered during BFS
	vector<vector<int>> m_bfsEdges;
	// For each node, a bit for whether we've seen it during BFS
	vector<bool> m_seenNode;

	// Total number of edges
	int m_numEdges = 0;

	OnTopologyEdgeChangeDispatcher m_onEdgeChange;
};

} // namespace csolver