// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once

#include <EASTL/hash_map.h>

#include "topology/ITopology.h"
#include "topology/DigraphTopology.h"

namespace Vertexy
{

// Vertex storage class for EdgeTopology. Stores linkage to the original graph.
struct EdgeTopologyVertex : public DigraphVertex
{
	EdgeTopologyVertex()
		: sourceFrom(-1)
		, sourceTo(-1)
		, bidirectional(false)
	{
	}

	EdgeTopologyVertex(int sourceFrom, int sourceTo, bool bidirectional)
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
 *  Digraph formed by converting all the edges in a source graph into vertices. Bidirectional edges
 *  in the source graph are converted into a single vertex.
 *
 *  Creating an TEdgeTopology out of a topology allows you to assign values to edges in the source topology,
 *  and quickly translate an edge in the source graph to a vertex in the edge graph.
 *
 *  NOTE does not (currently) respond to edge additions/deletions in the source graph.
 */
class EdgeTopology : public TDigraphTopologyBase<EdgeTopology, EdgeTopologyVertex>
{
public:
	EdgeTopology(const shared_ptr<ITopology>& source, bool mergeBidirectional = true, bool connected = true)
		: m_source(source)
	{
		initialize(mergeBidirectional, connected);
	}

	EdgeTopology(const EdgeTopology& rhs) = delete;
	EdgeTopology(EdgeTopology&& rhs) = delete;

	// Given an edge from the source graph, get the corresponding vertex in our graph
	int getVertexForSourceEdge(int sourceFrom, int sourceTo) const
	{
		const int numOutgoing = m_source->getNumOutgoing(sourceFrom);
		for (int edgeIndex = 0; edgeIndex < numOutgoing; ++edgeIndex)
		{
			int destVertex;
			if (m_source->getOutgoingDestination(sourceFrom, edgeIndex, destVertex) && destVertex == sourceTo)
			{
				return getVertexForSourceEdgeIndex(sourceFrom, edgeIndex);
			}
		}
		return -1;
	}

	// Given a vertex in our graph, return the corresponding edge in the source graph
	inline void getSourceEdgeForVertex(int vertexIndex, int& sourceFrom, int& sourceTo, bool& bidirectional) const
	{
		auto& vertex = m_vertices[vertexIndex];
		sourceFrom = vertex.sourceFrom;
		sourceTo = vertex.sourceTo;
		bidirectional = vertex.bidirectional;
	}

	wstring vertexIndexToString(int vertexIndex) const
	{
		int sourceFrom, sourceTo;
		bool bidirectional;
		getSourceEdgeForVertex(vertexIndex, sourceFrom, sourceTo, bidirectional);

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
		// Create a vertex for each edge in the source. Bidirectional edges share a single vertex.
		hash_map<tuple<int, int>, int> edgeMap;
		const int numVertices = m_source->getNumVertices();
		for (int vertexIdx = 0; vertexIdx < numVertices; ++vertexIdx)
		{
			const int numOutgoing = m_source->getNumOutgoing(vertexIdx);
			for (int edgeIndex = 0; edgeIndex < numOutgoing; ++edgeIndex)
			{
				int destVertex;
				if (m_source->getOutgoingDestination(vertexIdx, edgeIndex, destVertex))
				{
					vxy_assert(destVertex != vertexIdx);

					tuple<int, int> edgeDesc;
					bool bidirectional = false;
					if (mergeBidirectional && m_source->hasEdge(destVertex, vertexIdx))
					{
						bidirectional = true;

						int minValue = vertexIdx < destVertex ? vertexIdx : destVertex;
						int maxValue = vertexIdx > destVertex ? vertexIdx : destVertex;
						edgeDesc = make_tuple(minValue, maxValue);
					}
					else
					{
						edgeDesc = make_tuple(vertexIdx, destVertex);
					}

					if (edgeMap.find(edgeDesc) == edgeMap.end())
					{
						const int newVertexIdx = m_vertices.size();
						m_vertices.push_back(EdgeTopologyVertex{vertexIdx, destVertex, bidirectional});

						edgeMap[edgeDesc] = newVertexIdx;
					}
					m_sourceEdgeToVertexMap[make_tuple(vertexIdx, edgeIndex)] = edgeMap[edgeDesc];
				}
			}
		}

		if (connected)
		{
			// Create edges between the vertices. The vertex representing an edge in the source graph has an edge representing
			// every vertex that the source vertex is adjacent to.
			for (int vertexIndex = 0; vertexIndex < numVertices; ++vertexIndex)
			{
				const int numOutgoing = m_source->getNumOutgoing(vertexIndex);
				for (int edgeIndex1 = 0; edgeIndex1 < numOutgoing; ++edgeIndex1)
				{
					int destVertex1;
					if (m_source->getOutgoingDestination(vertexIndex, edgeIndex1, destVertex1))
					{
						for (int edgeIndex2 = 0; edgeIndex2 < numOutgoing; ++edgeIndex2)
						{
							if (edgeIndex1 == edgeIndex2)
							{
								continue;
							}

							int destVertex2;
							if (m_source->getOutgoingDestination(vertexIndex, edgeIndex2, destVertex2))
							{
								addEdge(getVertexForSourceEdgeIndex(vertexIndex, edgeIndex1), getVertexForSourceEdgeIndex(vertexIndex, edgeIndex2));
							}
						}
					}
				}
			}
		}
	}

	inline int getVertexForSourceEdgeIndex(int sourceVertexIndex, int sourceEdgeIdx) const
	{
		return m_sourceEdgeToVertexMap.find(make_tuple(sourceVertexIndex, sourceEdgeIdx))->second;
	}

	hash_map<tuple<int, int>, int> m_sourceEdgeToVertexMap;
	const shared_ptr<ITopology> m_source;
};

} // namespace Vertexy