// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once
#include "topology/GraphRelations.h"

namespace Vertexy
{
	class PrefabManager;
	class Tile;

	struct Position
	{
		int x, y;
	};

	struct NeighborData
	{
		const wstring right, left, above, below;
		vector<int> rightTiles, leftTiles, aboveTiles, belowTiles;

		NeighborData(const wstring& inRight, const wstring& inLeft, const wstring& inAbove, const wstring& inBelow) :
			right(inRight),
			left(inLeft),
			above(inAbove),
			below(inBelow)
		{
		};

		NeighborData() :
			right(TEXT("")),
			left(TEXT("")),
			above(TEXT("")),
			below(TEXT(""))
		{
		};
	};

	class Prefab
	{
	public:
		static const int INVALID_TILE = -1;
		static const int NO_PREFAB_ID = 0;
		static const int NO_PREFAB_POS = 0;

		Prefab(int inID, const vector<vector<Tile>> inTiles, const NeighborData& neighborData = NeighborData());

		// Returns the <x,y> grid position for the index-th tile in this prefab
		const Position& getPositionForIndex(int index);

		// Returns the tile value located at <x,y> in the grid
		int getTileValAtPos(int x, int y);

		// Returns the number of tiles in this prefab
		int getNumTiles();

		// Rotate the prefab 90 degrees clockwise N times
		void rotate(int times = 1);

		// Mirror the prefab horizontally
		void reflect();

		// Check if prefab has the same tile configuration as the other prefab
		bool isEqual(const Prefab& other);

		// Check if another prefab overlaps given a offset
		bool canOverlap(const Prefab& other, int dx, int dy);


		// Getters
		const int id() const { return m_id; };
		const vector<Position>& positions() const { return m_positions; };
		const vector<vector<Tile>>& tiles() const { return m_tiles; };
		const NeighborData& neighborData() const { return m_neighborData; };

	private:
		// This prefab's identifier, uniquely assigned by its manager
		int m_id;

		// The tiles that make up the Prefab, in 2D array form; the indices represent the tile type
		vector<vector<Tile>> m_tiles;

		// A vector of {x,y} positions; each element of this vector represents the position at which that index's tile in the prefab is found
		vector<Position> m_positions;

		// Stores this prefab's neighbors and the tiles that can have adjacent prefabs
		NeighborData m_neighborData;

		// Transpose the prefab
		void transpose();

		// Reverse the prefab row wise
		void reverse();

		// Set m_positions
		void updatePositions();

		// Set neighbor tiles in m_neighborData
		void updateNeighbors();
	};
}