// Copyright Proletariat, Inc. All Rights Reserved.
#include "prefab/PrefabManager.h"

#include "ConstraintSolver.h"
#include "ConstraintTypes.h"
#include "prefab/Prefab.h"
#include "variable/SolverVariableDomain.h"

using namespace Vertexy;

PrefabManager::PrefabManager(ConstraintSolver* inSolver, shared_ptr<PlanarGridTopology> inGrid)
{
	maxPrefabSize = 0;
	tilePrefabData = nullptr;
	tilePrefabPosData = nullptr;

	solver = inSolver;
	grid = inGrid;
}

void PrefabManager::CreatePrefab(vector<vector<int>> inTiles)
{
	// Create the prefab with its unique ID
	Prefab prefab = Prefab(prefabs.size() + 1, this, inTiles);

	// Update the largest size for the domain
	if (prefab.getNumTiles() > maxPrefabSize)
	{
		maxPrefabSize = prefab.getNumTiles();
	}

	// Add to our internal list of prefabs
	prefabs.push_back(prefab);
}

void PrefabManager::GeneratePrefabConstraints(shared_ptr<TTopologyVertexData<VarID>> tileData)
{
	// Create the domains
	SolverVariableDomain prefabDomain(Prefab::NO_PREFAB_ID, prefabs.size()); // NO_PREFAB_ID represents a tile with no prefab
	SolverVariableDomain positionDomain(Prefab::NO_PREFAB_POS, maxPrefabSize); // NO_PREFAB_POS is reserved for tiles with no prefab

	// Create the variable graphs
	tilePrefabData = solver->makeVariableGraph(TEXT("TilePrefabVars"), ITopology::adapt(grid), prefabDomain, TEXT("TilePrefabID"));
	tilePrefabPosData = solver->makeVariableGraph(TEXT("TilePrefabPosVars"), ITopology::adapt(grid), positionDomain, TEXT("TilePrefabPos"));

	auto selfTilePrefab = make_shared<TVertexToDataGraphRelation<VarID>>(tilePrefabData);
	auto selfTilePrefabPos = make_shared<TVertexToDataGraphRelation<VarID>>(tilePrefabPosData);

	// No prefab constraint
	solver->makeGraphConstraint<ClauseConstraint>(grid, ENoGood::NoGood,
		GraphRelationClause(selfTilePrefab, { Prefab::NO_PREFAB_ID }),
		GraphRelationClause(selfTilePrefabPos, EClauseSign::Outside, { Prefab::NO_PREFAB_POS })
		);

	// Prefab Constraints
	for (auto prefab : prefabs)
	{
		prefab.GeneratePrefabConstraints(*solver, grid, tileData);
	}
}

shared_ptr<TTopologyVertexData<VarID>> PrefabManager::getTilePrefabData()
{
	return tilePrefabData;
}

shared_ptr<TTopologyVertexData<VarID>> PrefabManager::getTilePrefabPosData()
{
	return tilePrefabPosData;
}

vector<Prefab> PrefabManager::getPrefabs()
{
	return prefabs;
}

int PrefabManager::getMaxPrefabSize()
{
	return maxPrefabSize;
}

void PrefabManager::CreateDefaultTestPrefab(int index)
{
	switch (index)
	{
	case 0: 
		CreatePrefab({ {0, 0},
									 {1, 1} });
		break;

	case 1:
		CreatePrefab({ { 1, -1, 1 },
									 {-1, -1, -1},
									 {1, -1, -1} });
		break;

	default: vxy_assert(false);
	}

}