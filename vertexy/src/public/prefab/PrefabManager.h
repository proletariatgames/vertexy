// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once
#include "topology/GraphRelations.h"
#include "topology/GridTopology.h"
#include "topology/IPlanarTopology.h"

namespace Vertexy
{
	class Prefab;
	class Tile;

	class PrefabManager : public enable_shared_from_this<PrefabManager>
	{
	public:
		PrefabManager(const PrefabManager& rhs) = delete;
		PrefabManager(PrefabManager&& rhs) = delete;
		PrefabManager& operator=(const PrefabManager & rhs) = delete;

		static shared_ptr<PrefabManager> create(ConstraintSolver* inSolver, const shared_ptr<PlanarGridTopology>& inGrid);
		
		// Creates a prefab and associates it with this manager
		void createPrefab(const vector<vector<Tile>>& inTiles, bool allowRotation = false, bool allowReflection = false);

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
		PrefabManager(ConstraintSolver* inSolver, const shared_ptr<PlanarGridTopology>& inGrid);

		// The prefabs associated with this manager
		vector<shared_ptr<Prefab>> m_prefabs;

		// The largest prefab associated with this manager (in terms of number of tiles in the prefab)
		int m_maxPrefabSize;

		// The original prefabs rotation/reflection associated with this manager
		// It is counted as a different list to avoid shifting posterior prefabs ids
		vector<shared_ptr<Prefab>> m_prefabs_configs;

		// Correlation map for original prefabs and their rotated/reflected states
		hash_map<int, vector<int>> prefabStateMap;

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