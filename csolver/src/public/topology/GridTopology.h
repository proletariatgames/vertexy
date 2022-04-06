// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once

#include "PlanarTopology.h"
#include "topology/Topology.h"
#include "topology/TopologyLink.h"

namespace csolver
{

/** Base class for 2D/3D grids. Handles everything except logic for which edges are crossable, which is delegated to Impl. */
template <typename Base, typename Impl>
class TGridTopologyBase : public Base
{
public:
	enum EDirections
	{
		Left = 0,
		Right = 1,
		Up = 2,
		Down = 3,
		Out = 4,
		In = 5
	};

	// Convenience functions for creating FTopologyLinks for different movements
	static TopologyLink moveLeft(int num = 1) { return TopologyLink::create(make_tuple(EDirections::Left, num)); }
	static TopologyLink moveRight(int num = 1) { return TopologyLink::create(make_tuple(EDirections::Right, num)); }
	static TopologyLink moveUp(int num = 1) { return TopologyLink::create(make_tuple(EDirections::Up, num)); }
	static TopologyLink moveDown(int num = 1) { return TopologyLink::create(make_tuple(EDirections::Down, num)); }
	static TopologyLink moveIn(int num = 1) { return TopologyLink::create(make_tuple(EDirections::In, num)); }
	static TopologyLink moveOut(int num = 1) { return TopologyLink::create(make_tuple(EDirections::Out, num)); }

	TGridTopologyBase(int32_t inWidth, int32_t inHeight, int32_t inDepth = 1)
		: m_width(inWidth)
		, m_height(inHeight)
		, m_depth(inDepth)
		, m_numDirections(inDepth > 1 ? 6 : 4)
	{
	}

	inline static int oppositeDirection(int dir)
	{
		switch (dir)
		{
		case Left:
			return Right;
		case Right:
			return Left;
		case Up:
			return Down;
		case Down:
			return Up;
		case Out:
			return In;
		default:
			cs_assert(dir == In);
			return Out;
		}
	}

	inline bool getAdjacent(int index, int direction, int dist, int& outIndex) const
	{
		int32_t x, y, z;
		indexToCoordinate(index, x, y, z);
		cs_sanity(inBounds(x,y,z));
		cs_assert(dist >= 0);

		switch (direction)
		{
		case Left: x -= dist;
			if (x < 0)
			{
				goto fail;
			}
			break;
		case Right: x += dist;
			if (x >= m_width)
			{
				goto fail;
			}
			break;
		case Up: y -= dist;
			if (y < 0)
			{
				goto fail;
			}
			break;
		case Down: y += dist;
			if (y >= m_height)
			{
				goto fail;
			}
			break;
		case In: z -= dist;
			if (z < 0)
			{
				goto fail;
			}
			break;
		case Out: z += dist;
			if (z >= m_depth)
			{
				goto fail;
			}
			break;
		default: cs_fail();
		}

		outIndex = coordinateToIndex(x, y, z);
		return true;

		fail:
			outIndex = -1;
		return false;
	}

	void getTopologyLinkOffset(const TopologyLink& link, int& x, int& y, int& z) const
	{
		x = 0;
		y = 0;
		z = 0;
		for (auto& dir : link.getDirections())
		{
			switch (dir.direction)
			{
			case Left: x -= dir.distance;
				break;
			case Right: x += dir.distance;
				break;
			case Up: y -= dir.distance;
				break;
			case Down: y += dir.distance;
				break;
			case In: z -= dir.distance;
				break;
			case Out: z += dir.distance;
				break;
			default: cs_fail();
			}
		}
	}

	bool areTopologyLinksEquivalent(const TopologyLink& first, const TopologyLink& second) const
	{
		int x1, y1, z1, x2, y2, z2;
		getTopologyLinkOffset(first, x1, y1, z1);
		getTopologyLinkOffset(second, x2, y2, z2);
		return (x1 == x2 && y1 == y2 && z1 == z2);
	}

	inline int getNumIncoming(int) const { return m_numDirections; }
	inline int getNumOutgoing(int) const { return m_numDirections; }

	inline int getNumNodes() const { return m_width * m_height * m_depth; }

	inline bool isValidNode(int node) const { return node >= 0 && node < getNumNodes(); }

	// Convenience function get a neighbor by coordinates
	inline bool getNeighbor(int32_t x, int32_t y, int32_t z, int direction, int numSteps, int32_t& nx, int32_t& ny, int32_t& nz) const
	{
		int outIndex;
		if (getNeighbor(coordinateToIndex(x, y, z), direction, numSteps, outIndex))
		{
			indexToCoordinate(outIndex, nx, ny, nz);
			return true;
		}
		return false;
	}

	// Convenience function to get a neighbor by 2D coordinates
	inline bool getNeighbor(int32_t x, int32_t y, int direction, int numSteps, int32_t& nx, int32_t& ny) const
	{
		cs_assert(m_depth == 1);
		int32_t nz;
		return getNeighbor(x, y, 0, direction, numSteps, nx, ny, nz);
	}

	// Convenience function to get the 2D coordinates for an index
	inline void indexToCoordinate(int index, int32_t& x, int32_t& y) const
	{
		cs_assert(m_depth == 1);
		int32_t z;
		indexToCoordinate(index, x, y, z);
	}

	inline int coordinateToIndex(int32_t x, int32_t y, int32_t z = 0) const
	{
		return (z * m_width * m_height) + (y * m_width) + x;
	}

	inline void indexToCoordinate(int index, int32_t& x, int32_t& y, int32_t& z) const
	{
		x = index % m_width;
		int32_t rem = index / m_width;
		y = rem % m_height;
		z = rem / m_height;
	}

	inline bool inBounds(int32_t x, int32_t y, int32_t z) const
	{
		return x >= 0 && y >= 0 && z >= 0 &&
			x < m_width && y < m_height && z < m_depth;
	}

	bool getTopologyLink(int startIndex, int endIndex, TopologyLink& outLink) const
	{
		int32_t sx, sy, sz, ex, ey, ez;
		indexToCoordinate(startIndex, sx, sy, sz);
		indexToCoordinate(endIndex, ex, ey, ez);

		outLink.clear();
		if (sx < ex)
		{
			outLink.append(EDirections::Right, ex - sx);
		}
		else if (sx > ex)
		{
			outLink.append(EDirections::Left, sx - ex);
		}

		if (sy < ey)
		{
			outLink.append(EDirections::Down, ey - sy);
		}
		else if (sy > ey)
		{
			outLink.append(EDirections::Up, sy - ey);
		}

		if (sz < ez)
		{
			outLink.append(EDirections::Out, ez - sz);
		}
		else if (sz > ez)
		{
			outLink.append(EDirections::In, sz - ez);
		}
		return true;
	}

	wstring nodeIndexToString(int nodeIndex) const
	{
		int32_t x, y, z;
		indexToCoordinate(nodeIndex, x, y, z);

		wstring name;
		if (m_depth > 1)
		{
			name.sprintf(TEXT("%dX%dX%d"), x, y, z);
		}
		else if (m_width > 1 && m_height > 1)
		{
			name.sprintf(TEXT("%dX%d"), x, y);
		}
		else if (m_width > 1)
		{
			name.sprintf(TEXT("%d"), x);
		}
		else
		{
			name.sprintf(TEXT("%d"), y);
		}
		return name;
	}

	wstring edgeIndexToString(int edgeIndex) const
	{
		switch (edgeIndex)
		{
		case Up:
			return TEXT("Up");
		case Down:
			return TEXT("Down");
		case Left:
			return TEXT("Left");
		case Right:
			return TEXT("Right");
		default:
			return TEXT("!Invalid!");
		}
	}

	inline OnTopologyEdgeChangeDispatcher& getEdgeChangeListener() { return m_onEdgeChange; }
	inline int GetWidth() const { return m_width; }
	inline int GetHeight() const { return m_height; }
	inline int GetDepth() const { return m_depth; }

protected:
	OnTopologyEdgeChangeDispatcher m_onEdgeChange;

	const int32_t m_width;
	const int32_t m_height;
	const int32_t m_depth;
	const int32_t m_numDirections;
};

// Simple version of grid topology where all adjacent neighbors are always connected
class SimpleGridTopology : public TGridTopologyBase<TTopology<SimpleGridTopology>, SimpleGridTopology>
{
public:
	SimpleGridTopology(int32_t inWidth, int32_t inHeight, int32_t inDepth = 1)
		: TGridTopologyBase(inWidth, inHeight, inDepth)
	{
	}

	inline bool getIncomingSource(int index, int edgeIdx, int& outIndex) const
	{
		return getAdjacent(index, oppositeDirection(edgeIdx), 1, outIndex);
	}

	inline bool getOutgoingDestination(int index, int edgeIdx, int& outIndex) const
	{
		return getAdjacent(index, edgeIdx, 1, outIndex);
	}

	inline bool getOutgoingDestination(int index, int edgeIdx, int distance, int& outIndex) const
	{
		return getAdjacent(index, edgeIdx, distance, outIndex);
	}

	bool hasEdge(int from, int to) const
	{
		if (from == to)
		{
			return false;
		}

		TopologyLink link;
		if (!getTopologyLink(from, to, link))
		{
			return false;
		}
		return link.getDirections().size() == 1 && link.getDirections()[0].distance == 1;
	}
};

// Planar version of grid topology. Depth is disallowed.
class PlanarGridTopology : public TGridTopologyBase<TPlanarTopology<PlanarGridTopology>, PlanarGridTopology>
{
public:
	PlanarGridTopology(int32_t inWidth, int32_t inHeight)
		: TGridTopologyBase(inWidth, inHeight, 1)
	{
	}

	inline bool getIncomingSource(VertexID index, int edgeIdx, VertexID& outIndex) const
	{
		return getAdjacent(index, oppositeDirection(edgeIdx), 1, outIndex);
	}

	inline bool getOutgoingDestination(VertexID index, int edgeIdx, VertexID& outIndex) const
	{
		return getAdjacent(index, edgeIdx, 1, outIndex);
	}

	inline bool getOutgoingDestination(VertexID index, int edgeIdx, int numTimes, VertexID& outIndex) const
	{
		return getAdjacent(index, edgeIdx, numTimes, outIndex);
	}

	bool hasEdge(VertexID from, VertexID to) const
	{
		if (from == to)
		{
			return false;
		}

		TopologyLink link;
		if (!getTopologyLink(from, to, link))
		{
			return false;
		}
		return link.getDirections().size() == 1 && link.getDirections()[0].distance == 1;
	}

	int getNumFaces() const { return (m_width - 1) * (m_height - 1); }

	int getNumEdges() const
	{
		const int outerSidesEdges = m_width - 1;
		const int outerTopBottomEdges = m_height - 1;
		const int interiorSidesEdges = (m_width - 2) * 2;
		const int interiorTopBottomEdges = (m_height - 2) * 2;
		return outerSidesEdges + outerTopBottomEdges + interiorSidesEdges + interiorTopBottomEdges;
	}

	int getNumFaceEdges(FaceID) const { return 4; }

	EdgeID getFaceEdgeByIndex(FaceID face, int edgeIdx) const { return (face << 2) + edgeIdx; }

	VertexID getEdgeDestination(EdgeID edge) const
	{
		constexpr int dirX[] = {0, 1, 1, 0};
		constexpr int dirY[] = {0, 0, 1, 1};

		FaceID face = getEdgeFace(edge);
		int edgeOffset = edge - (face << 2);

		int faceX = (face % (m_width - 1));
		int faceY = (face / (m_width - 1));
		return coordinateToIndex(faceX + dirX[edgeOffset], faceY + dirY[edgeOffset]);
	}

	inline EdgeID getReverseEdge(EdgeID edge) const
	{
		int reverseEdge;
		getEdgeOpposingFace(edge, reverseEdge);
		return reverseEdge;
	}

	inline FaceID getEdgeFace(EdgeID edge) const
	{
		return edge >> 2;
	}

	inline FaceID getEdgeOpposingFace(EdgeID edge) const
	{
		int unused;
		return getEdgeOpposingFace(edge, unused);
	}

	FaceID getEdgeOpposingFace(EdgeID edge, EdgeID& outReverseEdge) const
	{
		constexpr int dirX[] = {-1, 0, 1, 0};
		constexpr int dirY[] = {0, -1, 0, 1};
		constexpr int reversedEdgeOffsets[] = {2, 3, 0, 1};

		FaceID face = getEdgeFace(edge);
		int edgeBase = face << 2;
		int faceX = (face % (m_width - 1)) + dirX[edge - edgeBase];
		int faceY = (face / (m_width - 1)) + dirY[edge - edgeBase];

		if (faceX < 0 || faceX >= m_width - 1 || faceY < 0 || faceY >= m_height - 2)
		{
			outReverseEdge = -1;
			return -1;
		}

		FaceID newFace = faceY * (m_width - 1) + faceX;
		outReverseEdge = (newFace << 2) + reversedEdgeOffsets[edge];
		return newFace;
	}

	EdgeID getNextEdge(EdgeID edge) const
	{
		int base = getEdgeFace(edge) << 2;
		int edgeIdx = edge - base;
		return base + ((edgeIdx + 1) % 4);
	}
};

} // namespace csolver