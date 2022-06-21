// Copyright Proletariat, Inc. All Rights Reserved.
#include "prefab/TileSolver.h"
#include "ConstraintSolver.h"
#include <EASTL/vector.h>
#include <EASTL/set.h>
#include <EASTL/tuple.h>
#include <fstream>
#include <nlohmann/json.hpp>
#include "prefab/Tile.h"
#include "prefab/Prefab.h"
#include <sstream>
#include "topology/GridTopology.h"
#include "topology/IPlanarTopology.h"
#include "util/Asserts.h"

using namespace Vertexy;
using json = nlohmann::json;

TileSolver::TileSolver(ConstraintSolver* solver, int numCols, int numRows, int kernelSize, bool rotation, bool reflection):
	m_solver(solver),
	m_kernelSize(kernelSize),
	m_allowRotation(rotation),
	m_allowReflection(reflection)
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

	// Parse Tiles.
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

	// Parse input grid.
	vector<vector<Tile>> inputGrid;
	for (int y = 0; y < j["grid"].size(); y++)
	{
		inputGrid.push_back({});
		for (int x = 0; x < j["grid"][y].size(); x++)
		{
			inputGrid[y].push_back(Tile(*idMap[j["grid"][y][x]], j["config"][y][x]));
		}
	}
	createConstraints(inputGrid);
}

void TileSolver::createConstraints(const vector<vector<Tile>>& inputGrid)
{
	// Extract all NxN tiles from the input. 
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
			// Add prefab to a list. If it is not unique, increment the frequency weight.
			auto p = make_shared<Prefab>(m_prefabs.size() + 1, kernel);		
			addUnique(p);
			// If rotation/reflection is allowed, do the same for the prefab variations.
			if (m_allowReflection)
			{
				addPrefabVariation(p, 0, true);
			}
			if (m_allowRotation)
			{
				for (int k = 1; k < 4; k++)
				{
					addPrefabVariation(p, k, false);
					if (m_allowReflection)
					{
						addPrefabVariation(p, k, true);
					}
				}
			}
		}
	}

	// Get possible overlaps for cardinal direction offsets.
	for (int i = 0; i < m_prefabs.size(); i++)
	{
		for (int j = i; j < m_prefabs.size(); j++)
		{
			for (int x = -(m_kernelSize - 1); x < m_kernelSize; x++)
			{
				if (x == 0) { continue; }
				if (m_prefabs[i]->canOverlap(*m_prefabs[j], x, 0))
				{
					m_overlaps[m_prefabs[i]->id()][make_tuple(x, 0)].insert(m_prefabs[j]->id());
					m_overlaps[m_prefabs[j]->id()][make_tuple(-x, 0)].insert(m_prefabs[i]->id());
				}
			}
			for (int y = -(m_kernelSize - 1); y < m_kernelSize; y++)
			{
				if (y == 0) { continue; }
				if (m_prefabs[i]->canOverlap(*m_prefabs[j], 0, y))
				{
					m_overlaps[m_prefabs[i]->id()][make_tuple(0, y)].insert(m_prefabs[j]->id());
					m_overlaps[m_prefabs[j]->id()][make_tuple(0, -y)].insert(m_prefabs[i]->id());
				}
			}
		}
	}

	// Create domain and variable graphs.
	SolverVariableDomain domain(1, m_prefabs.size());
	m_tileData = m_solver->makeVariableGraph(TEXT("Vars"), ITopology::adapt(m_grid), domain, TEXT("prefabID"));
	
	// Create a map for grid offsets to graph relations.
	hash_map<tuple<int, int>, shared_ptr<TTopologyLinkGraphRelation<VarID>>> offsets;
	auto igrid = ITopology::adapt(m_grid);
	auto selfTile = make_shared<TVertexToDataGraphRelation<VarID>>(igrid, m_tileData);
	for (int x = -(m_kernelSize - 1); x < m_kernelSize; x++)
	{
		if (x == 0) { continue; }
		offsets[tuple<int, int>(x, 0)] = x < 0 ?
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

	// Add overlap constraints.
	for (const auto& [id, dirOverlap] : m_overlaps)
	{
		for (const auto& [dir, vars] : dirOverlap)
		{
			m_solver->makeGraphConstraint<ClauseConstraint>(m_grid, ENoGood::NoGood,
				GraphRelationClause(selfTile, { id }),
				GraphRelationClause(offsets[dir], EClauseSign::Outside, vector<int>(vars.begin(), vars.end()))
			);
		}
	}
}

// Creates a new prefab with rotation and reflection and try to add to a unique prefab list.
void TileSolver::addPrefabVariation(const shared_ptr<Prefab>& prefab, int rotations, bool reflection)
{
	auto pr = make_shared<Prefab>(m_prefabs.size() + 1, prefab->tiles());
	if (reflection)
	{
		pr->reflect();
	}
	pr->rotate(rotations);
	addUnique(pr);
}

// Tries to add a prefab to a list, if the prefab with the same configuration
// already exists on the list, increase the frequency weight for that prefab.
void TileSolver::addUnique(const shared_ptr<Prefab>& p)
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
	j["grid_cols"] = m_grid->getWidth();
	j["grid_rows"] = m_grid->getHeight();
	auto tileArray = json::array();
	for (auto const& tile : m_tiles)
	{
		tileArray.push_back({ {"id", tile->id()}, {"name", tile->name().c_str()} });
	}
	j["tiles"] = tileArray;
	auto gridArray = json::array();
	auto configArray = json::array();
	for (int y = 0; y < m_grid->getHeight(); ++y)
	{
		auto gridRowArray = json::array();
		auto configRowArray = json::array();
		for (int x = 0; x < m_grid->getHeight(); ++x)
		{
			int node = m_grid->coordinateToIndex(x, y);
			vector<int> potentialValues = m_solver->getPotentialValues(m_tileData->get(node));
			if (potentialValues.size() == 1)
			{
				int tileVal = potentialValues[0];
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