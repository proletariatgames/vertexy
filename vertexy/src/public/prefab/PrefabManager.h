// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once

#include "topology/GraphRelations.h"
#include "topology/GridTopology.h"
#include "topology/IPlanarTopology.h"

namespace Vertexy
{
	class Prefab;

	class PrefabManager
	{
	private:
		// The prefabs associated with this manager
		vector<Prefab> prefabs;

		// The largest prefab associated with this manager (in terms of number of tiles in the prefab)
		int maxPrefabSize;

		// The solver for which the prefab constraints will be created
		ConstraintSolver* solver;

		// The grid used for the tiles
		shared_ptr<PlanarGridTopology> grid;

		// The variable graph representing prefab ID's
		shared_ptr<TTopologyVertexData<VarID>> tilePrefabData;

		// The variable graph representing indices of the tiles within the prefab
		shared_ptr<TTopologyVertexData<VarID>> tilePrefabPosData;

	public:
		PrefabManager(ConstraintSolver* inSolver, shared_ptr<PlanarGridTopology> inGrid);

		// Creates a prefab and associates it with this manager
		void CreatePrefab(vector<vector<int>> inTiles);

		// Generates constraints for all prefabs associated with this manager
		void GeneratePrefabConstraints(shared_ptr<TTopologyVertexData<VarID>> tileData);

		// Returns the variable graph representing prefab ID's
		shared_ptr<TTopologyVertexData<VarID>> getTilePrefabData();

		// Returns the variable graph representing indices of the tiles within the prefab
		shared_ptr<TTopologyVertexData<VarID>> getTilePrefabPosData();

		// Returns this manager's prefabs
		vector<Prefab> getPrefabs();

		// Returns the size of the largest prefab associated with this manager
		int getMaxPrefabSize();

		// Create some basic sample prefabs used for testing
		void CreateDefaultTestPrefab(int index);
	};
}