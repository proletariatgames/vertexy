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

	class Prefab
	{
	public:
		static const int INVALID_TILE = -1;
		static const int NO_PREFAB_ID = 0;
		static const int NO_PREFAB_POS = 0;

		Prefab(int inID, const vector<vector<Tile>>& inTiles);

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

		// Getters
		const int id() const { return m_id; };
		const vector<Position>& positions() const { return m_positions; };
		const vector<vector<Tile>>& tiles() const { return m_tiles; };

	private:
		// The tiles that make up the Prefab, in 2D array form; the indices represent the tile type
		vector<vector<Tile>> m_tiles;

		// A vector of {x,y} positions; each element of this vector represents the position at which that index's tile in the prefab is found
		vector<Position> m_positions;

		// This prefab's identifier, uniquely assigned by its manager
		int m_id;

		// Transpose the prefab
		void transpose();

		// Reverse the prefab row wise
		void reverse();

		// Set m_positions
		void updatePositions();
	};
}