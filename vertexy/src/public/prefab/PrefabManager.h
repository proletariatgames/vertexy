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
	public:
		PrefabManager(ConstraintSolver* inSolver, const shared_ptr<PlanarGridTopology>& inGrid);
		
		PrefabManager(const PrefabManager& rhs) = delete;
		PrefabManager(PrefabManager&& rhs) = default;
		PrefabManager& operator=(const PrefabManager & rhs) = delete;
		
		// Creates a prefab and associates it with this manager
		void createPrefab(const vector<vector<int>>& inTiles);

		// Generates constraints for all prefabs associated with this manager
		void generatePrefabConstraints(const shared_ptr<TTopologyVertexData<VarID>>& tileData);

		// Returns the variable graph representing prefab ID's
		const shared_ptr<TTopologyVertexData<VarID>>& getTilePrefabData();

		// Returns the variable graph representing indices of the tiles within the prefab
		const shared_ptr<TTopologyVertexData<VarID>>& getTilePrefabPosData();

		// Returns this manager's prefabs
		const vector<shared_ptr<Prefab>>& getPrefabs();

		// Returns the size of the largest prefab associated with this manager
		int getMaxPrefabSize();

		// Create some basic sample prefabs used for testing
		void createDefaultTestPrefab(int index);

	private:
		// A shared_ptr to this
		shared_ptr<PrefabManager> m_thisPtr;

		// The prefabs associated with this manager
		vector<shared_ptr<Prefab>> m_prefabs;

		// The largest prefab associated with this manager (in terms of number of tiles in the prefab)
		int m_maxPrefabSize;

		// The solver for which the prefab constraints will be created
		ConstraintSolver* m_solver;

		// The grid used for the tiles
		shared_ptr<PlanarGridTopology> m_grid;

		// The variable graph representing prefab ID's
		shared_ptr<TTopologyVertexData<VarID>> m_tilePrefabData;

		// The variable graph representing indices of the tiles within the prefab
		shared_ptr<TTopologyVertexData<VarID>> m_tilePrefabPosData;
	};
}