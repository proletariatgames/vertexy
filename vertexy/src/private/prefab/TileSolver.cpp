// Copyright Proletariat, Inc. All Rights Reserved.
#include "prefab/TileSolver.h"
#include "ConstraintSolver.h"
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>
#include "prefab/Tile.h"
#include "prefab/Prefab.h"

#include <EASTL/vector.h>
#include <EASTL/set.h>
#include <EASTL/tuple.h>
#include "util/Asserts.h"

using namespace Vertexy;
using json = nlohmann::json;

TileSolver::TileSolver(ConstraintSolver* solver, int numCols, int numRows, int kernelSize, bool rotation, bool reflection):
	m_solver(solver),
	m_kernelSize(kernelSize),
	m_allowRotation(rotation),
	m_allowrefletion(reflection)
{
	m_grid = make_shared<PlanarGridTopology>(numCols, numRows);
}

void TileSolver::parseJsonFile(string filepath)
{
	if (!std::filesystem::exists(filepath.c_str()))
	{
		vxy_assert_msg(false, "Error! File path passed to parseJsonFile does not exist!");
	}
	std::ifstream file;
	file.open(filepath.c_str());
	std::stringstream strStream;
	strStream << file.rdbuf();
	parseJsonString(strStream.str().c_str());
}

void TileSolver::parseJsonString(string str)
{
	auto j = json::parse(str.c_str());

	// Parse Tiles
	hash_map<int, shared_ptr<Tile>> idMap;
	for (const auto& elem : j["tiles"])
	{
		auto tile = make_shared<Tile>(
			elem["id"].get<int>(),
			elem.value("name", "").c_str(),
			elem.value("symmetry", "X")[0]
			);
		m_tiles.push_back(tile);
		idMap[elem["id"].get<int>()] = tile;
	}

	// Parse input grid
	vector<vector<Tile>> inputGrid;
	for (int y = 0; y < j["grid"].size(); y++)
	{
		inputGrid.push_back({});
		for (int x = 0; x < j["grid"][y].size(); x++)
		{
			inputGrid[y].push_back(Tile(*idMap[j["grid"][y][x]], j["config"][y][x]));
		}
	}
	//DEBUG
	//for (const auto& row : inputGrid)
	//{
	//	for (const auto& col : row)
	//	{
	//		printf("(% d, % d) ", col.id(), col.configuration());
	//	}
	//	printf("\n");
	//}	
	printf("Finished parsing\n");
	createConstrains(inputGrid);
}

void TileSolver::createConstrains(const vector<vector<Tile>>& inputGrid)
{
	// Get all unique kernels
	int h = inputGrid.size();
	int w = inputGrid[0].size();
	for (int y = 0; y < h; y++)
	{
		for (int x = 0; x < w; x++)
		{
			vector<vector<Tile>> kernel;
			for (int i = 0; i < m_kernelSize; i++)
			{
				kernel.push_back({});
				for (int j = 0; j < m_kernelSize; j++)
				{
					int ri = i + y >= h ? (y + i) - h : y + i;
					int rj = j + x >= w ? (x + j) - w : x + j;
					kernel[i].push_back(inputGrid[ri][rj]);
				}
			}
			// add prefab to a list, if it is not unique, increment the frequency weight
			auto p = make_shared<Prefab>(m_prefabs.size() + 1, kernel);		
			addUnique(p);
			// If rotation/reflection is allowed, do the same for the prefab variants
			if (m_allowRotation)
			{
				for (int k = 1; k < 4; k++) { addPrefabVariation(p, k, false); }
				if (m_allowrefletion)
				{
					for (int k = 0; k < 4; k++) { addPrefabVariation(p, k, true); }
				}
			}
			else if (m_allowrefletion)
			{
				addPrefabVariation(p, 0, true);
			}
		}
	}
	//DEBUG
	//printf("Total Tiles: %d\n", m_prefabs.size());
	//for (const auto& p : m_prefabs)
	//{
	//	printf("(%d) w:%d\n", p->id(), m_prefabFreq[p->id()]);
	//	for (int i = 0; i < p->tiles().size(); i++)
	//	{
	//		for (int j = 0; j < p->tiles()[0].size(); j++)
	//		{
	//			printf("%d", p->tiles()[i][j].id());
	//		}
	//		printf("\n");
	//	}
	//}

	// get possible overlaps for cardinal directions
	hash_map<int, hash_map<tuple<int, int>, set<int>>> overlaps;
	for (int i = 0; i < m_prefabs.size(); i++)
	{
		for (int j = i; j < m_prefabs.size(); j++)
		{
			for (int x = -(m_kernelSize - 1); x < m_kernelSize; x++)
			{
				if (x == 0) { continue; }
				if (m_prefabs[i]->canOverlap(*m_prefabs[j], x, 0))
				{
					overlaps[m_prefabs[i]->id()][tuple<int, int>(x, 0)].insert(m_prefabs[j]->id());
					overlaps[m_prefabs[j]->id()][tuple<int, int>(-x, 0)].insert(m_prefabs[i]->id());
				}
			}
			for (int y = -(m_kernelSize - 1); y < m_kernelSize; y++)
			{
				if (y == 0) { continue; }
				if (m_prefabs[i]->canOverlap(*m_prefabs[j], 0, y))
				{
					overlaps[m_prefabs[i]->id()][tuple<int, int>(0, y)].insert(m_prefabs[j]->id());
					overlaps[m_prefabs[j]->id()][tuple<int, int>(0, -y)].insert(m_prefabs[i]->id());
				}
			}
		}
	}
	printf("Finished overlapping\n");
	//DEBUG
	//for (const auto& [id, val] : overlaps)
	//{
	//	printf("PREFAB %d:\n", id);
	//	for (const auto& [dir, m_prefabs] : val)
	//	{
	//		printf("   (%d,%d):\n", get<0>(dir), get<1>(dir));
	//		for (const auto& p : m_prefabs)
	//		{
	//			printf("   %d, ", p);
	//		}
	//		printf("\n");
	//	}
	//}

	//create domain
	SolverVariableDomain domain(1, m_prefabs.size());
	//make graph
	m_tileData = m_solver->makeVariableGraph(TEXT("Vars"), ITopology::adapt(m_grid), domain, TEXT("varID"));
	
	//create a map for offsets to graph relations
	hash_map<tuple<int, int>, shared_ptr<TTopologyLinkGraphRelation<VarID>>> offsets;
	auto igrid = ITopology::adapt(m_grid);
	auto selfTile = make_shared<TVertexToDataGraphRelation<VarID>>(igrid, m_tileData);
	for (int x = -(m_kernelSize - 1); x < m_kernelSize; x++)
	{
		if (x == 0) { continue; }
		offsets[tuple<int, int>(x, 0)] = x < 0?
			make_shared<TTopologyLinkGraphRelation<VarID>>(igrid, m_tileData, PlanarGridTopology::moveLeft(-x)) :
			make_shared<TTopologyLinkGraphRelation<VarID>>(igrid, m_tileData, PlanarGridTopology::moveRight(x));
	}
	for (int y = -(m_kernelSize - 1); y < m_kernelSize; y++)
	{
		if (y == 0) { continue; }
		offsets[tuple<int, int>(0, y)] = y < 0 ?
			make_shared<TTopologyLinkGraphRelation<VarID>>(igrid, m_tileData, PlanarGridTopology::moveUp(-y)) :
			make_shared<TTopologyLinkGraphRelation<VarID>>(igrid, m_tileData, PlanarGridTopology::moveDown(y));
	}

	// add overlap constraints
	for (const auto& [id, dirOverlap] : overlaps)
	{
		for (const auto& [dir, vars] : dirOverlap)
		{
			m_solver->makeGraphConstraint<ClauseConstraint>(m_grid, ENoGood::NoGood,
				GraphRelationClause(selfTile, { id }),
				GraphRelationClause(offsets[dir], EClauseSign::Outside, vector<int>(vars.begin(), vars.end()))
			);
		}
	}
	printf("Finished constraining\n");
	//add frequency constraints

}

void TileSolver::addPrefabVariation(shared_ptr<Prefab> prefab, int rotations, bool reflection)
{
	auto pr = make_shared<Prefab>(m_prefabs.size() + 1, prefab->tiles());
	if (reflection)
	{
		pr->reflect();
	}
	pr->rotate(rotations);
	addUnique(pr);
}

void TileSolver::addUnique(shared_ptr<Prefab> p)
{
	int equalId = -1;
	for (const auto& prefab : m_prefabs)
	{
		if (p->isEqual(*prefab))
		{
			equalId = prefab->id();
			break;
		}
	}
	if (equalId == -1)
	{
		m_prefabs.push_back(p);
		m_prefabFreq[p->id()] = 1;
	}
	else
	{
		m_prefabFreq[equalId]++;
	}
}

void TileSolver::exportJson(string path)
{
	json j;
	j["grid_cols"] = m_grid->GetWidth();
	j["grid_rows"] = m_grid->GetHeight();
	auto tileArray = json::array();
	for (auto const& tile : m_tiles)
	{
		tileArray.push_back({ {"id", tile->id()}, {"name", tile->name().c_str()} });
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
			vector<int> potentialValues = m_solver->getPotentialValues(m_tileData->get(node));
			if (potentialValues.size() == 1)
			{
				int tileVal = potentialValues[0];
				//int tileVal = m_solver->getSolvedValue(m_tileData->getData()[node]);
				gridRowArray.push_back(m_prefabs[tileVal - 1]->tiles()[0][0].id());
				configRowArray.push_back(m_prefabs[tileVal - 1]->tiles()[0][0].configuration());
			}
			else
			{
				gridRowArray.push_back(-1);
				configRowArray.push_back(-1);
			}
		}
		gridArray.push_back(gridRowArray);
		configArray.push_back(configRowArray);
	}
	j["grid"] = gridArray;
	j["config"] = configArray;
	std::ofstream o(path.c_str());
	o << std::setw(4) << j << std::endl;
}