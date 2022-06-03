// Copyright Proletariat, Inc. All Rights Reserved.
#include "prefab/PrefabManager.h"

#include "ConstraintSolver.h"
#include "ConstraintTypes.h"
#include "prefab/Prefab.h"
#include "variable/SolverVariableDomain.h"

#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>

using namespace Vertexy;
using json = nlohmann::json;

/*static*/ shared_ptr<PrefabManager> PrefabManager::create(ConstraintSolver* inSolver, const shared_ptr<PlanarGridTopology>& inGrid)
{
	auto retval = shared_ptr<PrefabManager>(new PrefabManager(inSolver, inGrid));
	return retval;
}

PrefabManager::PrefabManager(ConstraintSolver* inSolver, const shared_ptr<PlanarGridTopology>& inGrid)
{
	m_maxPrefabSize = 0;
	m_tilePrefabData = nullptr;
	m_tilePrefabPosData = nullptr;

	m_solver = inSolver;
	m_grid = inGrid;
}

void PrefabManager::createPrefab(const vector<vector<int>>& inTiles)
{
	// Create the prefab with its unique ID
	shared_ptr<Prefab> prefab = make_shared<Prefab>(m_prefabs.size() + 1, shared_from_this(), inTiles);

	// Update the largest size for the domain
	if (prefab->getNumTiles() > m_maxPrefabSize)
	{
		m_maxPrefabSize = prefab->getNumTiles();
	}

	// Add to our internal list of prefabs
	m_prefabs.push_back(prefab);
}

void PrefabManager::createPrefabFromJson(string filePath)
{
	// Ensure the file exists
	if (!std::filesystem::exists(filePath.c_str()))
	{
		vxy_assert_msg(false, "Error! File path passed to createPrefabFromJson does not exist!");
	}

	// Open the file and convert its contents to a string
	std::ifstream file;
	file.open(filePath.c_str());
	std::stringstream strStream;
	strStream << file.rdbuf();

	// Parse the json string and extract variables
	auto j = json::parse(strStream.str().c_str());
	vector<vector<int>> tiles;

	for (const auto& elem : j["tiles"])
	{
		vector<int> newRow;
		for (const int& tile : elem)
		{
			newRow.push_back(tile);
		}
		tiles.push_back(newRow);
	}

	// Ensure tiles isn't empty
	if (tiles.size() == 0 || tiles[0].size() == 0)
	{
		vxy_assert_msg(false, "Error! Json file passed to createPrefabFromJson contains no tiles!");
	}

	createPrefab(tiles);
}

void PrefabManager::generatePrefabConstraints(const shared_ptr<TTopologyVertexData<VarID>>& tileData)
{
	// Create the domains
	SolverVariableDomain prefabDomain(Prefab::NO_PREFAB_ID, m_prefabs.size()); // NO_PREFAB_ID represents a tile with no prefab
	SolverVariableDomain positionDomain(Prefab::NO_PREFAB_POS, m_maxPrefabSize); // NO_PREFAB_POS is reserved for tiles with no prefab

	// Create the variable graphs
	m_tilePrefabData = m_solver->makeVariableGraph(TEXT("TilePrefabVars"), ITopology::adapt(m_grid), prefabDomain, TEXT("TilePrefabID"));
	m_tilePrefabPosData = m_solver->makeVariableGraph(TEXT("TilePrefabPosVars"), ITopology::adapt(m_grid), positionDomain, TEXT("TilePrefabPos"));

	auto selfTilePrefab = make_shared<TVertexToDataGraphRelation<VarID>>(ITopology::adapt(m_grid), m_tilePrefabData);
	auto selfTilePrefabPos = make_shared<TVertexToDataGraphRelation<VarID>>(ITopology::adapt(m_grid), m_tilePrefabPosData);

	// No prefab constraint
	m_solver->makeGraphConstraint<ClauseConstraint>(m_grid, ENoGood::NoGood,
		GraphRelationClause(selfTilePrefab, { Prefab::NO_PREFAB_ID }),
		GraphRelationClause(selfTilePrefabPos, EClauseSign::Outside, { Prefab::NO_PREFAB_POS })
	);

	// Prefab Constraints
	for (auto& prefab : m_prefabs)
	{
		prefab->generatePrefabConstraints(m_solver, m_grid, tileData);
	}
}

const shared_ptr<TTopologyVertexData<VarID>>& PrefabManager::getTilePrefabData()
{
	return m_tilePrefabData;
}

const shared_ptr<TTopologyVertexData<VarID>>& PrefabManager::getTilePrefabPosData()
{
	return m_tilePrefabPosData;
}

const vector<shared_ptr<Prefab>>& PrefabManager::getPrefabs()
{
	return m_prefabs;
}

int PrefabManager::getMaxPrefabSize()
{
	return m_maxPrefabSize;
}

void PrefabManager::createDefaultTestPrefab(int index)
{
	switch (index)
	{
	case 0: 
		createPrefab({
			{0, 0},
			{1, 1}
		});
		break;

	case 1:
		createPrefab({
			{ 1, -1, 1 },
			{-1, -1, -1},
			{ 1, -1, -1}
		});
		break;

	default: vxy_assert(false);
	}

}