// Copyright Proletariat, Inc. All Rights Reserved.
#include "prefab/PrefabManager.h"

#include "ConstraintSolver.h"
#include "ConstraintTypes.h"
#include "prefab/Prefab.h"
#include "variable/SolverVariableDomain.h"
#include "prefab/Tile.h"

#include <codecvt>
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>

using namespace Vertexy;
using json = nlohmann::json;
using convert_type = std::codecvt_utf8<wchar_t>;
// Used to convert between std::string and std::wstring
std::wstring_convert<convert_type, wchar_t> strConverter;

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

void PrefabManager::createPrefab(const vector<vector<Tile>>& inTiles, const wstring& name /* "" */, bool allowRotation /* false */, bool allowReflection /* false */)
{
	// Create the prefab with its unique ID
	shared_ptr<Prefab> prefab = make_shared<Prefab>(m_prefabs.size() + 1, inTiles);

	// Update the largest size for the domain
	if (prefab->getNumTiles() > m_maxPrefabSize)
	{
		m_maxPrefabSize = prefab->getNumTiles();
	}

	// Add to our internal list of prefabs
	m_prefabs.push_back(prefab);

	// Add to the prefabStateMap
	if (!name.empty())
	{
		m_prefabStateMap.insert(name);
		m_prefabStateMap[name].push_back(prefab->id());
	}

	// if we allow rotation and/or reflection, create prefab configurations
	if (!allowRotation && !allowReflection)
	{
		return;
	}
	
	vector<shared_ptr<Prefab>> temp;
	if (allowRotation && allowReflection)
	{
		for (int i = 0; i < 7; i++) { temp.push_back(make_shared<Prefab>(m_prefabs.size() + 1 + i, inTiles)); }
		temp[0]->rotate(1);
		temp[1]->rotate(2);
		temp[2]->rotate(3);
		for (int j = 3; j < 7; j++) { temp[j]->reflect(); }
		temp[4]->rotate(1);
		temp[5]->rotate(2);
		temp[6]->rotate(3);
	}
	else if (allowRotation && !allowReflection)
	{
		for (int i = 0; i < 3; i++) { temp.push_back(make_shared<Prefab>(m_prefabs.size() + 1 + i, inTiles)); }
		temp[0]->rotate(1);
		temp[1]->rotate(2);
		temp[2]->rotate(3);
	}
	else
	{
		//Create horizontal and vertical reflections.
		for (int i = 0; i < 2; i++) { temp.push_back(make_shared<Prefab>(m_prefabs.size() + 1 + i, inTiles)); }
		temp[0]->reflect();
		temp[1]->rotate(2);
		temp[1]->reflect();
	}
	m_prefabs.insert(m_prefabs.end(), temp.begin(), temp.end());

	// Add to the prefabStateMap
	if (!name.empty())
	{
		for (int x = 1; x <= temp.size(); x++)
		{
			m_prefabStateMap[name].push_back(m_prefabStateMap[name][0] + x);
		}
	}
}

void PrefabManager::createPrefabFromJson(const wstring& filePath)
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
	std::wstring jsonString = strConverter.from_bytes(strStream.str());

	// Pass the string along to be parsed and converted to a prefab
	createPrefabFromJsonString(jsonString.c_str());
}

void PrefabManager::createPrefabFromJsonString(const wstring& jsonString)
{
	// Parse the json string and extract the tiles
	std::string stdJsonString = strConverter.to_bytes(jsonString.c_str());
	auto j = json::parse(stdJsonString);
	vector<vector<Tile>> tiles;

	for (const auto& elem : j["tiles"])
	{
		vector<Tile> newRow;
		for (const int& tile : elem)
		{
			Tile t(tile);
			newRow.push_back(t);
		}
		tiles.push_back(newRow);
	}

	// Ensure tiles isn't empty
	if (tiles.size() == 0 || tiles[0].size() == 0)
	{
		vxy_assert_msg(false, "Error! Json string passed to createPrefabFromJsonString contains no tiles!");
	}

	// Parse additional variables
	wstring name = TEXT("");
	bool allowRotation = false;
	bool allowReflection = false;

	if (j.contains("name"))
	{
		std::wstring jsonName = strConverter.from_bytes(j["name"]);
		name = jsonName.c_str();
	}

	if (j.contains("allowRotation"))
	{
		allowRotation = j["allowRotation"];
	}

	if (j.contains("allowReflection"))
	{
		allowReflection = j["allowReflection"];
	}

	createPrefab(tiles, name, allowRotation, allowReflection);
}

void PrefabManager::generatePrefabConstraints(const shared_ptr<TTopologyVertexData<VarID>>& tileData)
{
	// Create the domains
	SolverVariableDomain prefabDomain(Prefab::NO_PREFAB_ID, m_prefabs.size()); // NO_PREFAB_ID represents a tile with no prefab
	SolverVariableDomain positionDomain(Prefab::NO_PREFAB_POS, m_maxPrefabSize); // NO_PREFAB_POS is reserved for tiles with no prefab

	// Create the variable graphs
	m_tilePrefabData = m_solver->makeVariableGraph(TEXT("TilePrefabVars"), ITopology::adapt(m_grid), prefabDomain, TEXT("TilePrefabID"));
	m_tilePrefabPosData = m_solver->makeVariableGraph(TEXT("TilePrefabPosVars"), ITopology::adapt(m_grid), positionDomain, TEXT("TilePrefabPos"));

	auto selfTile = make_shared<TVertexToDataGraphRelation<VarID>>(ITopology::adapt(m_grid), tileData);
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
		// Ensure we have a real position if we have a prefab
		m_solver->makeGraphConstraint<ClauseConstraint>(m_grid, ENoGood::NoGood,
			GraphRelationClause(selfTilePrefab, { prefab->id() }),
			GraphRelationClause(selfTilePrefabPos, { Prefab::NO_PREFAB_POS })
		);

		// Ensure we don't use invalid values over this prefab's max
		for (int x = prefab->positions().size() + 1; x <= getMaxPrefabSize(); x++)
		{
			m_solver->makeGraphConstraint<ClauseConstraint>(m_grid, ENoGood::NoGood,
				GraphRelationClause(selfTilePrefab, { prefab->id() }),
				GraphRelationClause(selfTilePrefabPos, { x })
			);
		}

		for (int pos = 0; pos < prefab->positions().size(); pos++)
		{
			Position currLoc = prefab->positions()[pos];
			int id = prefab->id();

			// Self
			m_solver->makeGraphConstraint<ClauseConstraint>(m_grid, ENoGood::NoGood,
				GraphRelationClause(selfTile, EClauseSign::Outside, { prefab->tiles()[currLoc.x][currLoc.y].id() }),
				GraphRelationClause(selfTilePrefab, { id }),
				GraphRelationClause(selfTilePrefabPos, { pos + 1 })
			);

			// Prev
			if (pos > 0)
			{
				Position prevLoc = prefab->positions()[pos - 1];
				int diffX = currLoc.x - prevLoc.x;
				int diffY = currLoc.y - prevLoc.y;
				auto horizontalShift = make_shared<TopologyLinkIndexGraphRelation>(ITopology::adapt(m_grid), (diffY >= 0 ? PlanarGridTopology::moveLeft(diffY) : PlanarGridTopology::moveRight(-diffY)));
				auto verticalShift = make_shared<TopologyLinkIndexGraphRelation>(ITopology::adapt(m_grid), (diffX >= 0 ? PlanarGridTopology::moveUp(diffX) : PlanarGridTopology::moveDown(-diffX)));

				m_solver->makeGraphConstraint<ClauseConstraint>(m_grid, ENoGood::NoGood, GraphCulledVector<GraphRelationClause>::allOptional({
					GraphRelationClause(selfTilePrefab, { id }),
					GraphRelationClause(selfTilePrefabPos, { pos + 1 }),
					GraphRelationClause(horizontalShift->map(verticalShift)->map(selfTilePrefab), EClauseSign::Outside, { id })
				}));

				m_solver->makeGraphConstraint<ClauseConstraint>(m_grid, ENoGood::NoGood, GraphCulledVector<GraphRelationClause>::allOptional({
					GraphRelationClause(selfTilePrefab, { id }),
					GraphRelationClause(selfTilePrefabPos, { pos + 1 }),
					GraphRelationClause(horizontalShift->map(verticalShift)->map(selfTilePrefabPos), EClauseSign::Outside, { pos })
				}));
			}

			// Next
			if (pos < prefab->positions().size() - 1)
			{
				Position nextLoc = prefab->positions()[pos + 1];
				int diffX = currLoc.x - nextLoc.x;
				int diffY = currLoc.y - nextLoc.y;
				auto horizontalShift = make_shared<TopologyLinkIndexGraphRelation>(ITopology::adapt(m_grid), (diffY >= 0 ? PlanarGridTopology::moveLeft(diffY) : PlanarGridTopology::moveRight(-diffY)));
				auto verticalShift = make_shared<TopologyLinkIndexGraphRelation>(ITopology::adapt(m_grid), (diffX >= 0 ? PlanarGridTopology::moveUp(diffX) : PlanarGridTopology::moveDown(-diffX)));

				m_solver->makeGraphConstraint<ClauseConstraint>(m_grid, ENoGood::NoGood, GraphCulledVector<GraphRelationClause>::allOptional({
					GraphRelationClause(selfTilePrefab, { id }),
					GraphRelationClause(selfTilePrefabPos, { pos + 1 }),
					GraphRelationClause(horizontalShift->map(verticalShift)->map(selfTilePrefab), EClauseSign::Outside, { id })
				}));

				m_solver->makeGraphConstraint<ClauseConstraint>(m_grid, ENoGood::NoGood, GraphCulledVector<GraphRelationClause>::allOptional({
					GraphRelationClause(selfTilePrefab, { id }),
					GraphRelationClause(selfTilePrefabPos, { pos + 1 }),
					GraphRelationClause(horizontalShift->map(verticalShift)->map(selfTilePrefabPos), EClauseSign::Outside, { pos + 2 })
				}));
			}
		}
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

const vector<int>& PrefabManager::getPrefabIdsByName(const wstring& name)
{
	if (name.empty() || m_prefabStateMap.find(name) == m_prefabStateMap.end())
	{
		vxy_assert_msg(false, "Error! Invalid prefab name passed to getPrefabIdsByName");
	}

	return m_prefabStateMap[name];
}

int PrefabManager::getMaxPrefabSize()
{
	return m_maxPrefabSize;
}

void PrefabManager::createDefaultTestPrefab(int index, const wstring& name, bool rot, bool refl)
{
	Tile tx(-1);
	Tile t0(0);
	Tile t1(1);
	switch (index)
	{
	case 0: 
		createPrefab({
			{ t0, t0 },
			{ t1, t1 }
		}, name, rot, refl);
		break;

	case 1:
		createPrefab({
			{ t1, tx, t1 },
			{ tx, tx, tx },
			{ t1, tx, tx }
		}, name, rot, refl);
		break;

	default: vxy_assert(false);
	}
}