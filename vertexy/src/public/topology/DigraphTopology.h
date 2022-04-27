// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once

#include "ConstraintTypes.h"
#include "algo/ShortestPath.h"
#include "topology/Topology.h"
#include "topology/TopologyLink.h"

namespace Vertexy
{

/** Minimal implementation of a vertex in a directed graph */
struct DigraphVertex
{
	// Edges coming out of this vertex, pointing to index of destination vertex
	vector<int> outEdges;
	// Edges coming into this vertex, pointing to index of source vertex
	vector<int> inEdges;

	void reset()
	{
		outEdges.clear();
		inEdges.clear();
	}
};

/** Implementation of topology for simple directed graphs */
template <typename Impl, typename VertexType=DigraphVertex>
class TDigraphTopologyBase : public TTopology<Impl>
{
public:
	TDigraphTopologyBase()
	{
	}

	inline bool isValidVertex(int vertexIndex) const { return vertexIndex >= 0 && vertexIndex < m_vertices.size(); }
	inline int getNumOutgoing(int vertex) const { return m_vertices[vertex].outEdges.size(); }
	inline int getNumIncoming(int vertex) const { return m_vertices[vertex].inEdges.size(); }
	inline int getNumVertices() const { return m_vertices.size(); }

	inline bool hasEdge(int from, int to) const
	{
		auto& edges = m_vertices[from].outEdges;
		return contains(edges.begin(), edges.end(), to);
	}

	bool getOutgoingDestination(int vertexIndex, int edgeIndex, int& outVertex) const
	{
		auto& vertex = m_vertices[vertexIndex];
		if (edgeIndex >= vertex.outEdges.size())
		{
			return false;
		}
		outVertex = vertex.outEdges[edgeIndex];
		return true;
	}

	bool getOutgoingDestination(int vertexIndex, int edgeIndex, int numTimes, int& outVertex) const
	{
		outVertex = vertexIndex;
		for (int i = 0; i < numTimes; ++i)
		{
			int nextVertex;
			if (!getOutgoingDestination(outVertex, edgeIndex, nextVertex))
			{
				return false;
			}
			outVertex = nextVertex;
		}
		return true;
	}

	bool getIncomingSource(int vertexIndex, int edgeIndex, int& outVertex) const
	{
		auto& vertex = m_vertices[vertexIndex];
		if (edgeIndex >= vertex.inEdges.size())
		{
			return false;
		}
		outVertex = vertex.inEdges[edgeIndex];
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

		#ifdef VERTEXY_SANITY_CHECKS
		{
			int checkDest;
			vxy_verify(outLink.resolve(*this, startIndex, checkDest));
			vxy_sanity(checkDest == endIndex);
		}
		#endif

		return true;
	}

	wstring vertexIndexToString(int vertexIndex) const { return {wstring::CtorSprintf(), TEXT("%d"), vertexIndex}; }
	wstring edgeIndexToString(int edgeIndex) const { return {wstring::CtorSprintf(), TEXT("%d"), edgeIndex}; }

	int addVertex()
	{
		m_vertices.push_back({});
		return m_vertices.size() - 1;
	}

	void reset(int numVertices)
	{
		m_vertices.clear();
		m_vertices.resize(numVertices);
	}

	void addEdge(int vertexFrom, int vertexTo)
	{
		if (!contains(m_vertices[vertexFrom].outEdges.begin(), m_vertices[vertexFrom].outEdges.end(), vertexTo))
		{
			m_vertices[vertexFrom].outEdges.push_back(vertexTo);
		}
		if (!contains(m_vertices[vertexTo].inEdges.begin(), m_vertices[vertexTo].inEdges.end(), vertexFrom))
		{
			m_vertices[vertexTo].inEdges.push_back(vertexFrom);
		}
		m_onEdgeChange.broadcast(true, vertexFrom, vertexTo);
	}

	void removeEdge(int vertexFrom, int vertexTo)
	{
		int edgeIdx = indexOf(m_vertices[vertexFrom].outEdges.begin(), m_vertices[vertexFrom].outEdges.end(), vertexTo);
		if (edgeIdx >= 0)
		{
			m_vertices[vertexFrom].outEdges.erase_unsorted(&m_vertices[vertexFrom].outEdges[edgeIdx]);
			m_vertices[vertexTo].inEdges.erase_first_unsorted(vertexFrom);
			m_onEdgeChange.broadcast(false, vertexFrom, vertexTo);
		}
	}

	OnTopologyEdgeChangeDispatcher& getEdgeChangeListener() { return m_onEdgeChange; }

protected:
	vector<VertexType> m_vertices;
	OnTopologyEdgeChangeDispatcher m_onEdgeChange;
};

/** Instantiation of directed graph topology */
class DigraphTopology : public TDigraphTopologyBase<DigraphTopology, DigraphVertex>
{
public:
	DigraphTopology()
	{
	}
};

} // namespace Vertexy