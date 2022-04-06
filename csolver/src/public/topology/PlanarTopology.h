// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once

#include "topology/Topology.h"

namespace csolver
{

class IPlanarTopology;
template <typename T, typename Itf>
class TPlanarTopologyAdapter;

/* Interface for subclass of topologies that are planar, i.e. can be drawn on a 2D plane. */
template <typename Impl>
class TPlanarTopology : public TTopology<Impl>
{
	friend class IPlanarTopology;
	using TTopology<Impl>::NodeID;

public:
	using AdapterType = TPlanarTopologyAdapter<Impl, IPlanarTopology>;
	using VertexID = NodeID;
	using FaceID = int;
	using EdgeID = int;

	// Total number of faces (areas surrounded by clockwise edges)
	inline int getNumFaces() const { return static_cast<const Impl*>(this)->getNumFaces(); }
	// Total number of edges
	inline int getNumEdges() const { return static_cast<const Impl*>(this)->getNumEdges(); }
	// Number of edges is the given face
	inline int getNumFaceEdges(FaceID face) const { return static_cast<const Impl*>(this)->getNumFaceEdges(face); }
	// Nth edge of a face
	inline EdgeID getFaceEdgeByIndex(FaceID face, int edgeIdx) const { return static_cast<const Impl*>(this)->getFaceEdgeByIndex(face, edgeIdx); }
	// Node/vertex where the edge ends
	inline VertexID getEdgeDestination(EdgeID edge) const { return static_cast<const Impl*>(this)->getEdgeDestination(edge); }
	// Edge on opposing face, going in reverse direction (or -1 if no opposing face)
	inline EdgeID getReverseEdge(EdgeID edge) const { return static_cast<const Impl*>(this)->getReverseEdge(edge); }
	// Right-face of the edge
	inline FaceID getEdgeFace(EdgeID edge) const { return static_cast<const Impl*>(this)->getEdgeFace(edge); }
	// Left-face of the edge
	inline FaceID getEdgeOpposingFaces(EdgeID edge) const { return static_cast<const Impl*>(this)->getEdgeOpposingFace(edge); }
	// Left-face of the edge, also returning the corresponding edge on that face
	inline FaceID getEdgeOpposingFace(EdgeID edge, EdgeID& outReverseEdge) const { return static_cast<const Impl*>(this)->getEdgeOpposingFace(edge, outReverseEdge); }
	// Next edge in the loop of edges forming a face
	inline EdgeID getNextEdge(EdgeID edge) const { return static_cast<const Impl*>(this)->getNextEdge(edge); }
};

} // namespace csolver