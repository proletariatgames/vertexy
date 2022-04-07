// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once

#include "ConstraintTypes.h"
#include "topology/Topology.h"
#include "topology/TopologyLink.h"
#include <EASTL/bonus/ring_buffer.h>

namespace Vertexy
{

// Bipartite (two-sided) graph structure
// Essentially there are two sets of vertices: Left and Right, and edges connecting elements of Left to Right.
class BipartiteGraph : public TTopology<BipartiteGraph>
{
public:
	BipartiteGraph()
		: m_numLeft(0)
		, m_numRight(0)
	{
	}

	BipartiteGraph(int numLeftVertices, int numRightVertices, const vector<int>* inRightCapacities = nullptr)
	{
		initialize(numLeftVertices, numRightVertices, inRightCapacities);
	}

	//////////////////////////////////////
	// TTopology implementation

	bool isValidVertex(int index) const { return index >= 0 && index < m_numLeft + m_numRight; }
	int getNumVertices() const { return m_numLeft + m_numRight; }
	OnTopologyEdgeChangeDispatcher& getEdgeChangeListener() { return m_onEdgeChange; }

	int getNumOutgoing(int vertex) const
	{
		return (vertex < m_numLeft) ? m_adjLeft[vertex].size() : 0;
	}

	int getNumIncoming(int vertex)
	{
		return (vertex >= m_numLeft) ? m_adjRight[vertex - m_numLeft].size() : 0;
	}

	bool hasEdge(int from, int to) const
	{
		return from < m_numLeft
				   ? contains(m_adjLeft[from].begin(), m_adjLeft[from].end(), to - m_numLeft)
				   : 0;
	}

	bool getIncomingSource(int vertex, int edgeIndex, int& outVertex)
	{
		if (vertex >= m_numLeft)
		{
			outVertex = m_adjRight[vertex - m_numLeft][edgeIndex];
			return true;
		}
		else
		{
			outVertex = -1;
			return false;
		}
	}

	bool getOutgoingDestination(int vertex, int edgeIndex, int& outVertex) const
	{
		if (vertex < m_numLeft)
		{
			outVertex = m_adjLeft[vertex][edgeIndex];
			return true;
		}
		else
		{
			outVertex = -1;
			return false;
		}
	}

	bool getTopologyLink(int startVertex, int endVertex, TopologyLink& outLink) const
	{
		// Can only move from left vertex to connected right vertex; that's it.
		if (startVertex < m_numLeft)
		{
			int rightIdx = indexOf(m_adjLeft[startVertex].begin(), m_adjLeft[startVertex].end(), endVertex - m_numLeft);
			if (rightIdx >= 0)
			{
				outLink.append(0, 1);
				return true;
			}
		}
		return false;
	}

	wstring vertexIndexToString(int vertexIndex) const
	{
		if (vertexIndex < m_numLeft)
		{
			return {wstring::CtorSprintf(), TEXT("Left-%d"), vertexIndex};
		}
		else
		{
			return {wstring::CtorSprintf(), TEXT("Right-%d"), vertexIndex - m_numLeft};
		}
	}

	wstring edgeIndexToString(int edgeIndex) const
	{
		return {wstring::CtorSprintf(), TEXT("%d"), edgeIndex};
	}

	// TTopology implementation end
	//////////////////////////////////////

	void initialize(int numLeftVertices, int numRightVertices, const vector<int>* inRightCapacities = nullptr)
	{
		m_numLeft = numLeftVertices;
		m_numRight = numRightVertices;
		clearAllEdges();

		m_adjLeft.resize(numLeftVertices);
		for (int i = 0; i < m_adjLeft.size(); ++i)
		{
			m_adjLeft[i].clear();
		}

		m_adjRight.resize(numRightVertices);
		for (int i = 0; i < m_adjRight.size(); ++i)
		{
			m_adjRight[i].clear();
		}

		m_pairLeft.clear();
		m_pairLeft.resize(m_numLeft, -1);

		m_rightCapacities.clear();
		if (inRightCapacities)
		{
			vxy_assert(inRightCapacities->size() <= numRightVertices);
			m_rightCapacities.insert(m_rightCapacities.end(), inRightCapacities->begin(), inRightCapacities->end());
			while (m_rightCapacities.size() < m_numRight)
			{
				m_rightCapacities.push_back(0);
			}
		}

		m_matchedNumRight.clear();
		m_matchedNumRight.resize(numRightVertices, 0);
	}

	void reserveEdges(int leftVertex, int num)
	{
		m_adjLeft[leftVertex].reserve(num);
	}

	void addEdge(int leftVertex, int rightVertex)
	{
		vxy_sanity(!contains( m_adjLeft[leftVertex].begin(), m_adjLeft[leftVertex].end(), rightVertex));
		vxy_sanity(!contains( m_adjRight[rightVertex].begin(), m_adjRight[rightVertex].end(), leftVertex));
		m_adjLeft[leftVertex].push_back(rightVertex);
		m_adjRight[rightVertex].push_back(leftVertex);

		++m_numEdges;

		m_onEdgeChange.broadcast(true, leftVertex, m_numLeft + rightVertex);
	}

	bool hasBipartiteEdge(int leftVertex, int rightVertex) const
	{
		return contains(m_adjLeft[leftVertex].begin(), m_adjLeft[leftVertex].end(), rightVertex);
	}

	// Removes all edges originating from leftVertex
	void removeEdges(int left)
	{
		const int leftVertex = left;
		for (int i = 0; i < m_adjLeft[leftVertex].size(); ++i)
		{
			int rightVertex = m_adjLeft[leftVertex][i];
			vxy_assert(rightVertex >= 0);

			m_adjLeft[leftVertex][i] = 0;
			m_adjRight[rightVertex].erase_first_unsorted(leftVertex);
			m_onEdgeChange.broadcast(false, left, m_numLeft + rightVertex);

			m_numEdges--;
		}

		match(leftVertex, -1);
		vxy_assert(m_pairLeft[leftVertex] < 0);

		m_adjLeft[leftVertex].clear();
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
					vxy_assert(right >= 0);

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
	// @param leftVertexToDeprioritize If specified, the given left vertex will be matched after everything else is matched.
	void computeMaximalMatching(int leftVertexToDeprioritize = -1);

	// Incremental API.
	// Call RemoveEdges/AddEdge as necessary. Reuses the previous matching if one exists.
	// @return Whether a full matching has been found (i.e. all left vertices match with a right vertex)
	bool incrementalMaximalMatching(int leftVertexToDeprioritize = -1);

protected:
	int hopcroftBfs(int deprioritize, vector<int>& leftEdges);
	bool findAugmentingPath(int left);

	inline int getRightCapacity(int rightVertex) const
	{
		return m_rightCapacities.empty() ? 1 : m_rightCapacities[rightVertex];
	}

	inline bool isBelowCapacity(int rightVertex) const
	{
		return m_matchedNumRight[rightVertex] < getRightCapacity(rightVertex);
	}

	inline void match(int left, int right)
	{
		int prevRight = getMatchedRightSide(left);
		if (prevRight >= 0)
		{
			vxy_assert(m_matchedNumRight[prevRight] > 0);
			m_matchedNumRight[prevRight]--;
		}

		m_pairLeft[left] = right;
		if (right >= 0)
		{
			m_matchedNumRight[right]++;
			vxy_assert(m_matchedNumRight[right] <= getRightCapacity(right));
		}
	}

	// Number of vertices on the left side of the graph
	int m_numLeft;
	// Number of vertices on the right side of the graph
	int m_numRight;

	// Edges from Left->Right
	vector<vector<int>> m_adjLeft;
	vector<vector<int>> m_adjRight;

	// Pairings from Left->Right
	vector<int> m_pairLeft;

	// Capacities for each right vertex
	vector<int> m_rightCapacities;
	// How many are matched on right side
	vector<int> m_matchedNumRight;

	// FIFO queue for BFS
	ring_buffer<int> m_queue;
	// For each vertex, the list of outgoing vertices discovered during BFS
	vector<vector<int>> m_bfsEdges;
	// For each vertex, a bit for whether we've seen it during BFS
	vector<bool> m_seenVertex;

	// Total number of edges
	int m_numEdges = 0;

	OnTopologyEdgeChangeDispatcher m_onEdgeChange;
};

} // namespace Vertexy