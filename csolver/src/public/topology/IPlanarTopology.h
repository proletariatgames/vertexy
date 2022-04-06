// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once

#include "topology/ITopology.h"

namespace csolver
{

/** Generic interface for planar topologies.
 *
 *  To create from a raw topology, e.g.:
 *  shared_ptr<IPlanarTopology> TopoItf = IPlanarTopology::Adapt(Topo);
 */
class IPlanarTopology : public TPlanarTopology<IPlanarTopology>, public ITopology
{
public:
	using FaceID = int;
	using EdgeID = int;
	using VertexID = int;

	virtual int getNumFaces() const = 0;
	// Total number of edges
	virtual int getNumEdges() const = 0;
	// Number of edges is the given face
	virtual int getNumFaceEdges(FaceID face) const = 0;
	// Nth edge of a face
	virtual EdgeID getFaceEdgeByIndex(FaceID face, int edgeIdx) const = 0;
	// Node/vertex where the edge ends
	virtual VertexID getEdgeDestination(EdgeID edge) const = 0;
	// Edge on opposing face, going in reverse direction (or -1 if no opposing face)
	virtual EdgeID getReverseEdge(EdgeID edge) const = 0;
	// Right-face of the edge
	virtual FaceID getEdgeFace(EdgeID edge) const = 0;
	// Left-face of the edge
	virtual FaceID getEdgeOpposingFace(EdgeID edge) const = 0;
	// Left-face of the edge, also returning the corresponding edge on that face
	virtual FaceID getEdgeOpposingFace(EdgeID edge, EdgeID& outReverseEdge) const = 0;
	// Next edge in the loop of edges forming a face
	virtual EdgeID getNextEdge(EdgeID edge) const = 0;

	template <typename T>
	static shared_ptr<IPlanarTopology> adapt(const shared_ptr<T>& topology);
};

template <typename Impl, typename Itf=IPlanarTopology>
class TPlanarTopologyAdapter : public TTopologyAdapter<Impl, Itf>
{
public:
	TPlanarTopologyAdapter(const shared_ptr<Impl>& impl)
		: TTopologyAdapter(impl)
	{
	}

	using IPlanarTopology::FaceID;
	using IPlanarTopology::EdgeID;
	using IPlanarTopology::VertexID;

	virtual int getNumFaces() const override { return m_implementation->getNumFaces(); }
	virtual int getNumEdges() const override { return m_implementation->getNumEdges(); }
	virtual int getNumFaceEdges(FaceID face) const override { return m_implementation->getNumFaceEdges(face); }
	virtual EdgeID getFaceEdgeByIndex(FaceID face, int edgeIdx) const override { return m_implementation->getFaceEdgeByIndex(face, edgeIdx); }
	virtual VertexID getEdgeDestination(EdgeID edge) const override { return m_implementation->getEdgeDestination(edge); }
	virtual EdgeID getReverseEdge(EdgeID edge) const { return m_implementation->getReverseEdge(edge); }
	virtual FaceID getEdgeFace(EdgeID edge) const override { return m_implementation->getEdgeFace(edge); }
	virtual FaceID getEdgeOpposingFace(EdgeID edge) const override { return m_implementation->getEdgeOpposingFace(edge); }
	virtual FaceID getEdgeOpposingFace(EdgeID edge, EdgeID& outReverseEdge) const { return m_implementation->getEdgeOpposingFace(edge, outReverseEdge); }
	virtual EdgeID getNextEdge(EdgeID edge) const { return m_implementation->getNextEdge(edge); }
};

template <typename T>
shared_ptr<IPlanarTopology> IPlanarTopology::adapt(const shared_ptr<T>& topology)
{
	if (topology->m_itf.get() == nullptr)
	{
		topology->m_itf = make_shared<typename T::AdapterType>(topology);
	}
	return static_shared_pointer_cast<IPlanarTopology>(topology->m_itf);
}

} // namespace csolver