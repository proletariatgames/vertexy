// Copyright Proletariat, Inc. All Rights Reserved.
#include "Tileset/TileSolver.h"
#include "ConstraintSolver.h"
#include "topology/GridTopology.h"
#include "topology/IPlanarTopology.h"
#include "topology/GraphRelations.h"
#include "ConstraintTypes.h"
#include <fstream>

#include <nlohmann/json.hpp>
#include <EASTL/set.h>
#include <EASTL/iterator.h>

using namespace Vertexy;
using json = nlohmann::json;

TileSolver::D4Symmetry::D4Symmetry(char symmetry)
{
	vector<char> v = { 'X', 'I', '/', 'T', 'L', 'F' };
	vxy_assert((find(v.begin(), v.end(), symmetry) != v.end()));
	switch (symmetry)
	{
	case 'X':
		cardinality = 1;
		a = [](int i) { vxy_assert(i < 1); return 0; };
		b = [](int i) { vxy_assert(i < 1); return 0; };
		break;
	case 'I':
		cardinality = 2;
		a = [](int i) { vxy_assert(i < 2); return 1 - i; };
		b = [](int i) { vxy_assert(i < 2); return i; };
		break;
	case '/':
		cardinality = 2;
		a = [](int i) { vxy_assert(i < 2); return 1 - i; };
		b = [](int i) { vxy_assert(i < 2); return 1 - i; };
		break;
	case 'T':
		cardinality = 4;
		a = [](int i) { vxy_assert(i < 4); return (1 + i) % 4; };
		b = [](int i) { vxy_assert(i < 4); return i % 2 == 0 ? i : 4 - i; };
		break;
	case 'L':
		cardinality = 4;
		a = [](int i) { vxy_assert(i < 4); return (1 + i) % 4; };
		b = [](int i) { vxy_assert(i < 4); return 3 - i; };
		break;
	case 'F':
		cardinality = 8;
		a = [](int i) { vxy_assert(i < 8); return i < 4 ? (i + 1) % 4 : 4 + (i - 1) % 4; };
		b = [](int i) { vxy_assert(i < 8); return i < 4 ? i + 4 : i - 4; };
		break;
	default:
		cardinality = 1;
		a = [](int i) { vxy_assert(i < 1); return 0; };
		b = [](int i) { vxy_assert(i < 1); return 0; };
	}
}

TileSolver::Relationship* TileSolver::Relationship::a()
{
	c0 = t0->a(c0);
	c1 = t1->a(c1);
	cd = dir->a(cd);
	return this;
}

TileSolver::Relationship* TileSolver::Relationship::b()
{
	c0 = t0->b(c0);
	c1 = t1->b(c1);
	cd = dir->b(cd);
	return this;
}

TileSolver::TileSolver(ConstraintSolver* solver, int numCols, int numRows)
{
	m_solver = solver;
	m_grid = make_shared<PlanarGridTopology>(numCols, numRows);
}

void TileSolver::parseJsonFile(string filepath)
{

}

void TileSolver::parseJsonString(string str)
{
	auto j = json::parse(str.c_str());

	int tileID = 0;
	for (const auto& elem : j["tiles"])
	{
		this->m_tiles.push_back(make_shared<Tile>(tileID++,
			elem["name"].get<std::string>().c_str(),
			elem["symmetry"].get<std::string>()[0],
			elem.value("weight_min", 0.0),
			elem.value("weight_max", 1.0)));
	}

	// Cardinal directions has a symmetry of 'T'. Use other symmetries for other vectors (i.e. '/' for diagonals)
	shared_ptr<D4Symmetry> direction = make_shared<D4Symmetry>('T');
	
	vector<shared_ptr<Relationship>> rels;
	for (auto& elem : j["relations"])
	{
		auto rel = make_shared<Relationship>();
		rel->dir = direction;
		rels.push_back(rel);
		for (auto& [key, value] : elem.items())
		{
			int startConfiguration = 0;
			string name = value.get<std::string>().c_str();
			int pos = name.find(' ', 0);
			if (pos > -1)
			{
				startConfiguration = std::stoi(name.substr(pos + 1).c_str());
				name = name.substr(0, pos);
			}
			auto it = find_if(m_tiles.begin(), m_tiles.end(),
							  [&name](const auto& t)
							  {return t->name == name; });
			vxy_assert(it != m_tiles.end());
			auto tile = m_tiles[distance(m_tiles.begin(), it)];
			if (key == "self")
			{
				rel->t0 = tile;
				rel->c0 = startConfiguration;
			}
			else
			{
				rel->t1 = tile;
				rel->c1 = startConfiguration;
				if (key == "up") rel->cd = 0;
				if (key == "right") rel->cd = 1;
				if (key == "down") rel->cd = 2;
				if (key == "left") rel->cd = 3;
			}
		}
	}

	// Create a graph for tile and another for its rotation / reflection
	SolverVariableDomain tileDomain(0, m_tiles.size());
	SolverVariableDomain configDomain(0, 7);
	m_tileData = m_solver->makeVariableGraph(TEXT("TileVars"), ITopology::adapt(m_grid), tileDomain, TEXT("Tile"));
	m_configData = m_solver->makeVariableGraph(TEXT("ConfigVars"), ITopology::adapt(m_grid), configDomain, TEXT("Config"));

	// Create all possible relations based on the original relations set by the config file.
	createAllPossibleRelations(rels);
}

void TileSolver::createAllPossibleRelations(const vector<shared_ptr<Relationship>>& original_rel)
{
	// copy original relation then perform rotations/reflections for all configurations
	for (const auto& r : original_rel)
	{
		vector<shared_ptr<Relationship>> configs;
		for (int i = 0; i < 8; i++) { configs.push_back(make_shared<Relationship>(*r)); }
		configs[1]->a();
		configs[2]->a()->a();
		configs[3]->a()->a()->a();
		configs[4]->b();
		configs[5]->b()->a();
		configs[6]->b()->a()->a();
		configs[7]->b()->a()->a()->a();
		m_allRel.insert(end(m_allRel), begin(configs), end(configs));
	}
	createConstraints();
}

// Get possible neighbors tiles from a direction.
// Also add reciprocal relations with opposite direction.
vector<int> TileSolver::getAllowedTiles(int t0, int dir)
{
	set<int> unique;
	for (const auto& r : m_allRel)
	{
		if (r->t0->id == t0 && r->cd == dir)
		{
			unique.insert(r->t1->id);
		}
		int opositeDir = r->dir->a(r->dir->a(dir));
		if (r->t1->id == t0 && r->cd == opositeDir)
		{
			unique.insert(r->t0->id);
		}
	}
	return vector<int>(unique.begin(), unique.end());
}

//get possible neighbor configuration for a given tile, direction and configuration
vector<int> TileSolver::getAllowedconfigurations(int t0, int c0, int t1, int dir)
{
	set<int> unique;
	for (const auto& r : m_allRel)
	{
		if (r->t0->id == t0 && r->c0 == c0 && r->t1->id == t1 && r->cd == dir)
		{
			unique.insert(r->c1);
		}
		int opositeDir = r->dir->a(r->dir->a(dir));
		if (r->t0->id == t1 && r->t1->id == t0 && r->c1 == c0 && r->cd == opositeDir)
		{
			unique.insert(r->c0);
		}
	}
	return vector<int>(unique.begin(), unique.end());
}

//get possible tile cardinalities
vector<int> TileSolver::getAllowedCardinalities(int t0)
{
	set<int> unique;
	for (const auto& r : m_allRel)
	{
		if (r->t0->id == t0)
		{
			unique.insert(r->c0);
		}
		if (r->t1->id == t0)
		{
			unique.insert(r->c1);
		}
	}
	return vector<int>(unique.begin(), unique.end());
}

void TileSolver::createConstraints()
{
	auto selfTile = make_shared<TVertexToDataGraphRelation<VarID>>(m_tileData);
	vector<shared_ptr<TTopologyLinkGraphRelation<VarID>>> tileDirs = {
		make_shared<TTopologyLinkGraphRelation<VarID>>(m_tileData, PlanarGridTopology::moveUp()),
		make_shared<TTopologyLinkGraphRelation<VarID>>(m_tileData, PlanarGridTopology::moveRight()),
		make_shared<TTopologyLinkGraphRelation<VarID>>(m_tileData, PlanarGridTopology::moveDown()),
		make_shared<TTopologyLinkGraphRelation<VarID>>(m_tileData, PlanarGridTopology::moveLeft())
	};

	auto selfConfig = make_shared<TVertexToDataGraphRelation<VarID>>(m_configData);
	vector<shared_ptr<TTopologyLinkGraphRelation<VarID>>> configDirs = {
		make_shared<TTopologyLinkGraphRelation<VarID>>(m_configData, PlanarGridTopology::moveUp()),
		make_shared<TTopologyLinkGraphRelation<VarID>>(m_configData, PlanarGridTopology::moveRight()),
		make_shared<TTopologyLinkGraphRelation<VarID>>(m_configData, PlanarGridTopology::moveDown()),
		make_shared<TTopologyLinkGraphRelation<VarID>>(m_configData, PlanarGridTopology::moveLeft())
	};
	hash_map<int, tuple<int, int>> globalCardinalities;
	int totalTiles = m_grid->GetHeight() * m_grid->GetWidth();

	// for all tile types
	for (const auto& tile : m_tiles)
	{
		// for all directions
		for (int i = 0; i < 4; i++)
		{
			
			
			//create neighbor constrain for direction i
			auto neighbors = getAllowedTiles(tile->id, i);
			m_solver->makeGraphConstraint<ClauseConstraint>(m_grid, ENoGood::NoGood,
				GraphRelationClause(selfTile, { tile->id }),
				GraphRelationClause(tileDirs[i], EClauseSign::Outside, neighbors)
				);
			
			// create configuration range constrain
			auto tileConfigs = getAllowedCardinalities(tile->id);
			m_solver->makeGraphConstraint<ClauseConstraint>(m_grid, ENoGood::NoGood,
				GraphRelationClause(selfTile, { tile->id }),
				GraphRelationClause(selfConfig, EClauseSign::Outside, tileConfigs)
				);

			// given two tiles and a direction, constrain allowed rotations / reflections
			for (const auto& c : tileConfigs)
			{
				for (const auto& n : neighbors)
				{
					auto t1Configs = getAllowedconfigurations(tile->id, c, n, i);
					m_solver->makeGraphConstraint<ClauseConstraint>(m_grid, ENoGood::NoGood,
						GraphRelationClause(selfTile, { tile->id }),
						GraphRelationClause(tileDirs[i], { n }),
						GraphRelationClause(selfConfig, { c }),
						GraphRelationClause(configDirs[i], EClauseSign::Outside, t1Configs)
						);
					printf("%d-%d-%d %d(", tile->id, i, c, n);
					for (const auto& c1 : t1Configs)
					{
						printf("%d ", c1);
					}
					printf(")\n");
				}
			}
		}

		//add frequency constraint
		int r0 = (int)(max(0, min(totalTiles * (tile->weightMin), totalTiles)));
		int r1 = (int)(max(0, min(totalTiles * (tile->weightMax), totalTiles)));
		globalCardinalities[tile->id] = make_tuple(r0, r1);
	}
	m_solver->cardinality(m_tileData->getData(), globalCardinalities);
}

shared_ptr<PlanarGridTopology> TileSolver::grid()
{
	return m_grid;
}

shared_ptr<TTopologyVertexData<VarID>> TileSolver::tileData()
{
	return m_tileData;
}

shared_ptr<TTopologyVertexData<VarID>> TileSolver::configData()
{
	return m_configData;
}

void TileSolver::exportResults()
{
	json j;
	j["projection"] = "orthographic"; //PLACEHOLER
	j["tile_dimension"] = 10; //PLACEHOLER
	j["grid_cols"] = m_grid->GetWidth();
	j["grid_rows"] = m_grid->GetHeight();
	auto tileArray = json::array();
	for (auto const& tile : m_tiles)
	{
		tileArray.push_back({ {"id", tile->id}, {"name", tile->name.c_str()} });
	}
	j["tiles"] = tileArray;
	auto gridArray = json::array();
	auto configArray = json::array();
	for (int y = 0; y < m_grid->GetHeight(); ++y)
	{
		auto gridRowArray = json::array();
		auto configRowArray = json::array();
		for (int x = 0; x < m_grid->GetHeight(); ++x)
		{
			int node = m_grid->coordinateToIndex(x, y);
			int tileVal = m_solver->getSolvedValue(m_tileData->getData()[node]);
			gridRowArray.push_back(tileVal);
			int configVal = m_solver->getSolvedValue(m_configData->getData()[node]);
			configRowArray.push_back(configVal);
		}
		gridArray.push_back(gridRowArray);
		configArray.push_back(configRowArray);
	}
	j["grid"] = gridArray;
	j["config"] = configArray;
	std::ofstream o("pretty.json");
	o << std::setw(4) << j << std::endl;
}



