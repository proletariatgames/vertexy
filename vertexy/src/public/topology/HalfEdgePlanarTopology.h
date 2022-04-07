// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once

#include "ConstraintTypes.h"
#include "topology/PlanarTopology.h"

#include <EASTL/functional.h>

namespace Vertexy
{

/** Implementation of a generalized planar topology using half-edge data structure for efficient traversal */
class HalfEdgePlanarTopology : public TPlanarTopology<HalfEdgePlanarTopology>
{
	using TTopology<HalfEdgePlanarTopology>::VertexID;
public:
	struct HalfEdge
	{
		FaceID face;
		VertexID endVertex;
		EdgeID oppositeEdge;
	};

	struct FaceRecord
	{
		EdgeID firstEdge;
		int numEdges;
	};

	struct VertexRecord
	{
		EdgeID firstOutgoingEdge;
		int numOutgoing;
		EdgeID firstIncomingEdge;
		int numIncoming;
	};

	///////////////////////////////////////////////
	// TTopology implementation

	bool isValidVertex(VertexID vertex) const { return vertex >= 0 && vertex < m_vertices.size(); }
	int getNumOutgoing(VertexID vertex) const { return m_vertices[vertex].numOutgoing; }
	int getNumIncoming(VertexID vertex) const { return m_vertices[vertex].numIncoming; }
	int getNumVertices() const { return m_vertices.size(); }

	bool hasEdge(VertexID from, VertexID to) const
	{
		bool found = false;
		visitVertexOutgoingEdges(from, [&](EdgeID cur)
		{
			if (m_edges[cur].endVertex == to)
			{
				found = true;
				return false;
			}
			return true;
		});
		return found;
	}

	bool getIncomingSource(VertexID vertex, int edgeIndex, VertexID& outVertex) const
	{
		int i = 0;
		outVertex = -1;
		visitVertexIncomingEdges(vertex, [&](EdgeID cur)
		{
			if (i == edgeIndex)
			{
				outVertex = m_edges[cur].endVertex;
				return false;
			}
			++i;
			return true;
		});

		vxy_assert(outVertex >= 0);
		return true;
	}

	bool getOutgoingDestination(VertexID vertex, int edgeIndex, VertexID& outVertex) const
	{
		int i = 0;
		outVertex = -1;
		visitVertexOutgoingEdges(vertex, [&](EdgeID cur)
		{
			if (i == edgeIndex)
			{
				outVertex = m_edges[cur].endVertex;
				return false;
			}
			++i;
			return true;
		});

		vxy_assert(outVertex >= 0);
		return true;
	}

	bool areTopologyLinksEquivalent(const TopologyLink& first, const TopologyLink& second) const
	{
		return first == second;
	}

	bool getTopologyLink(VertexID startVertex, VertexID endVertex, TopologyLink& outLink) const
	{
		vector<VertexID> backLinks;
		backLinks.resize(getNumVertices());

		// Search source to destination. BackLinks stores the breadcrumbs to get from dest back to source.
		bool foundPath = false;
		DepthFirstSearchAlgorithm depthFirstSearchAlgorithm;
		auto searchCallback = [&](VertexID vertex, VertexID parent)
		{
			if (vertex == startVertex)
			{
				return ETopologySearchResponse::Continue;
			}

			backLinks[vertex] = parent;
			if (vertex == endVertex)
			{
				foundPath = true;
				return ETopologySearchResponse::Abort;
			}

			return ETopologySearchResponse::Continue;
		};
		depthFirstSearchAlgorithm.search(*this, endVertex, searchCallback);

		if (!foundPath)
		{
			return false;
		}

		// Go backwards from endVertex, recording the edges traversed
		vector<int> links;
		for (int cur = endVertex; cur != startVertex; cur = backLinks[cur])
		{
			int curIdx = 0;
			int foundEdgeIdx = -1;
			visitVertexOutgoingEdges(backLinks[cur], [&](EdgeID edge)
			{
				if (m_edges[edge].endVertex == cur)
				{
					foundEdgeIdx = curIdx;
					return false;
				}
				++curIdx;
				return true;
			});

			vxy_assert(foundEdgeIdx >= 0);
			links.push_back(foundEdgeIdx);
		}

		// Swap order
		for (int32_t i = 0, i2 = links.size() - 1; i < links.size() / 2 /*rounding down*/; ++i, --i2)
		{
			swap(links[i], links[i2]);
		}

		outLink.assign(links);
		return true;
	}

	OnTopologyEdgeChangeDispatcher& getEdgeChangeListener() { return m_onEdgeChange; }
	wstring vertexIndexToString(VertexID vertex) const { return {wstring::CtorSprintf(), TEXT("%d"), vertex}; }
	wstring edgeIndexToString(int edgeIndex) const { return {wstring::CtorSprintf(), TEXT("%d"), edgeIndex}; }

	///////////////////////////////////////////////
	// TPlanarTopology implementation

	int getNumFaces() const { return m_faces.size(); }
	int getNumEdges() const { return m_edges.size(); }
	int getNumFaceEdges(FaceID face) const { return m_faces[face].numEdges; }

	EdgeID getFaceEdgeByIndex(FaceID face, int edgeIdx) const
	{
		vxy_assert(edgeIdx >= 0 && edgeIdx < m_faces[face].numEdges);
		return m_faces[face].firstEdge + edgeIdx;
	}

	VertexID getEdgeDestination(EdgeID edge) const { return m_edges[edge].endVertex; }
	EdgeID getReverseEdge(EdgeID edge) const { return m_edges[edge].oppositeEdge; }
	FaceID getEdgeFace(EdgeID edge) const { return m_edges[edge].face; }
	FaceID getEdgeOpposingFace(EdgeID edge) const { return m_edges[m_edges[edge].oppositeEdge].face; }

	FaceID getEdgeOpposingFace(EdgeID edge, EdgeID& outReverseEdge) const
	{
		outReverseEdge = m_edges[edge].oppositeEdge;
		return m_edges[outReverseEdge].face;
	}

	EdgeID getNextEdge(EdgeID edge) const
	{
		const FaceRecord& face = m_faces[m_edges[edge].face];
		const int faceEdgeIdx = (edge - face.firstEdge) % face.numEdges;
		return face.firstEdge + faceEdgeIdx;
	}

	////////////////////
	// Custom methods

	const VertexRecord& getVertex(VertexID vertex) const { return m_vertices[vertex]; }
	const HalfEdge& getEdge(EdgeID edge) const { return m_edges[edge]; }
	const FaceRecord& getFace(FaceID face) const { return m_faces[face]; }

	template <typename T>
	void visitVertexOutgoingEdges(VertexID vert, T&& callback) const
	{
		const VertexRecord& vertex = m_vertices[vert];
		EdgeID edge = vertex.firstOutgoingEdge;
		do
		{
			if (!callback(edge))
			{
				break;
			}
			EdgeID reversedEdge = m_edges[edge].oppositeEdge;
			edge = getNextEdge(reversedEdge);
		} while (edge != vertex.firstOutgoingEdge);
	}

	void visitVertexIncomingEdges(VertexID vert, const function<bool(EdgeID)>& callback) const
	{
		const VertexRecord& vertex = m_vertices[vert];
		EdgeID edge = vertex.firstIncomingEdge;
		do
		{
			if (!callback(edge))
			{
				break;
			}
			EdgeID reversedEdge = m_edges[edge].oppositeEdge;
			edge = getNextEdge(reversedEdge);
		} while (edge != vertex.firstIncomingEdge);
	}

	void initialize(const vector<vector<int>>& faceVertices, int numVertices)
	{
		int numEdges = 0;
		for (auto& verts : faceVertices)
		{
			numEdges += verts.size();
			vxy_sanity(!containsPredicate(verts.begin(), verts.end(), [&](int v) { return v < 0 || v >= numVertices; }));
		}

		m_vertices.resize(numVertices, {-1, 0, -1, 0});
		m_edges.resize(numEdges);
		m_faces.resize(faceVertices.size());

		vector<int> startVerts;
		startVerts.resize(numEdges);

		// Build each face and edges
		EdgeID nextEdgeId = 0;
		for (int faceIdx = 0; faceIdx < faceVertices.size(); ++faceIdx)
		{
			const vector<int>& vertsForFace = faceVertices[faceIdx];
			m_faces[faceIdx].firstEdge = nextEdgeId;
			m_faces[faceIdx].numEdges = vertsForFace.size();

			for (int vertIdx = 0; vertIdx < vertsForFace.size(); ++vertIdx)
			{
				VertexID vPrevID = vertsForFace[((vertIdx - 1) + vertsForFace.size()) % vertsForFace.size()];
				VertexID vCurID = vertsForFace[vertIdx];

				m_edges[nextEdgeId].face = faceIdx;
				m_edges[nextEdgeId].endVertex = vCurID;
				startVerts[nextEdgeId] = vPrevID;

				VertexRecord& vPrev = m_vertices[vPrevID];
				VertexRecord& vCur = m_vertices[vCurID];
				if (vPrev.firstOutgoingEdge < 0)
				{
					vPrev.firstOutgoingEdge = nextEdgeId;
				}
				vPrev.numOutgoing++;
				if (vCur.firstIncomingEdge < 0)
				{
					vCur.firstOutgoingEdge = nextEdgeId;
				}
				vCur.numIncoming++;

				nextEdgeId++;
			}
		}

		// Hook up opposite edge links
		for (int i = 0; i < m_edges.size(); ++i)
		{
			if (m_edges[i].oppositeEdge >= 0)
			{
				continue;
			}

			const int32_t vertexIndex0 = startVerts[i];
			const int32_t vertexIndex1 = m_edges[i].endVertex;

			// Find the edge with the vertices the other way round
			for (int j = i + 1; j < m_edges.size(); ++j)
			{
				if (((m_edges[j].endVertex == vertexIndex0)) && (m_edges[j].oppositeEdge == vertexIndex1))
				{
					vxy_assert(m_edges[j].oppositeEdge < 0);
					m_edges[j].oppositeEdge = i;
					m_edges[i].oppositeEdge = j;
					break;
				}
			}
		}
	}

protected:
	vector<VertexRecord> m_vertices;
	vector<HalfEdge> m_edges;
	vector<FaceRecord> m_faces;
	OnTopologyEdgeChangeDispatcher m_onEdgeChange;
};

} // namespace Vertexy