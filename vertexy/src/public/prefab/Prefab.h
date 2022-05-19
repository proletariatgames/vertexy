// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once

#include "topology/GraphRelations.h"
#include "topology/GridTopology.h"
#include "topology/IPlanarTopology.h"

namespace Vertexy
{
	class PrefabManager;

	class Prefab
	{
	public:
		static const int INVALID_TILE = -1;
		static const int NO_PREFAB_ID = 0;
		static const int NO_PREFAB_POS = 0;

	private:
		// The tiles that make up the Prefab, in 2D array form; the indices represent the tile type
		vector<vector<int>> tiles;

		// A vector of {x,y} positions; each element of this vector represents the position at which that index's tile in the prefab is found
		vector<vector<int>> positions;

		// This prefab's identifier, uniquely assigned by its manager
		int id;

		// This prefab's manager
		PrefabManager* manager;

	public:
		Prefab(int inID, PrefabManager* inManager, vector<vector<int>> inTiles);

		// Given a solver, grid, and tileData, converts this prefab into a list of constraints and adds them to the solver
		void GeneratePrefabConstraints(ConstraintSolver& solver, shared_ptr<PlanarGridTopology> grid, shared_ptr<TTopologyVertexData<VarID>> tileData);

		// Returns the <x,y> grid position for the index-th tile in this prefab
		vector<int> getPositionForIndex(int index);

		// Returns the tile value located at <x,y> in the grid
		int getTileValAtPos(int x, int y);

		// Returns the number of tiles in this prefab
		int getNumTiles();
	};
}