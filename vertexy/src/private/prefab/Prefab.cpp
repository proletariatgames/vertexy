// Copyright Proletariat, Inc. All Rights Reserved.
#include "prefab/Prefab.h"

#include "ConstraintSolver.h"
#include "ConstraintTypes.h"
#include "prefab/PrefabManager.h"
#include "variable/SolverVariableDomain.h"
#include "prefab/Tile.h"

using namespace Vertexy;

Prefab::Prefab(int inID, const vector<vector<Tile>> inTiles, bool rotation, bool reflection):
	m_id(inID),
	m_tiles(inTiles)
{
	// Instantiate the positions
	for (int x = 0; x < m_tiles.size(); x++)
	{
		for (int y = 0; y < m_tiles[x].size(); y++)
		{
			// Skip elements that aren't in the prefab
			if (m_tiles[x][y].id() == INVALID_TILE)
			{
				continue;
			}

			m_positions.push_back(Position{ x,y });
		}
	}
}

void Prefab::generatePrefabConstraints(ConstraintSolver* solver, const shared_ptr<PlanarGridTopology>& grid, const shared_ptr<TTopologyVertexData<VarID>>& tileData)
{
	//auto selfTile = make_shared<TVertexToDataGraphRelation<VarID>>(ITopology::adapt(grid), tileData);
	//auto selfTilePrefab = make_shared<TVertexToDataGraphRelation<VarID>>(ITopology::adapt(grid), m_manager->getTilePrefabData());
	//auto selfTilePrefabPos = make_shared<TVertexToDataGraphRelation<VarID>>(ITopology::adapt(grid), m_manager->getTilePrefabPosData());

	//// Ensure we have a real position if we have a prefab
	//solver->makeGraphConstraint<ClauseConstraint>(grid, ENoGood::NoGood,
	//	GraphRelationClause(selfTilePrefab, { m_id }),
	//	GraphRelationClause(selfTilePrefabPos, { NO_PREFAB_POS })
	//);

	//// Ensure we don't use invalid values over this prefab's max
	//for (int x = m_positions.size() + 1; x <= m_manager->getMaxPrefabSize(); x++)
	//{
	//	solver->makeGraphConstraint<ClauseConstraint>(grid, ENoGood::NoGood,
	//		GraphRelationClause(selfTilePrefab, { m_id }),
	//		GraphRelationClause(selfTilePrefabPos, { x })
	//	);
	//}
	//
	//for (int pos = 0; pos < m_positions.size(); pos++)
	//{
	//	Position currLoc = m_positions[pos];

	//	// Self
	//	solver->makeGraphConstraint<ClauseConstraint>(grid, ENoGood::NoGood,
	//		GraphRelationClause(selfTile, EClauseSign::Outside, { m_tiles[currLoc.x][currLoc.y].id() }),
	//		GraphRelationClause(selfTilePrefab, { m_id }),
	//		GraphRelationClause(selfTilePrefabPos, { pos + 1 })
	//	);

	//	// Prev
	//	if (pos > 0)
	//	{
	//		Position prevLoc = m_positions[pos - 1];
	//		int diffX = currLoc.x - prevLoc.x;
	//		int diffY = currLoc.y - prevLoc.y;
	//		auto horizontalShift = make_shared<TopologyLinkIndexGraphRelation>(ITopology::adapt(grid), (diffY >= 0 ? PlanarGridTopology::moveLeft(diffY) : PlanarGridTopology::moveRight(-diffY)));
	//		auto verticalShift = make_shared<TopologyLinkIndexGraphRelation>(ITopology::adapt(grid), (diffX >= 0 ? PlanarGridTopology::moveUp(diffX) : PlanarGridTopology::moveDown(-diffX)));

	//		solver->makeGraphConstraint<ClauseConstraint>(grid, ENoGood::NoGood, GraphCulledVector<GraphRelationClause>::allOptional({
	//				GraphRelationClause(selfTilePrefab, { m_id }),
	//				GraphRelationClause(selfTilePrefabPos, { pos + 1 }),
	//				GraphRelationClause(horizontalShift->map(verticalShift)->map(selfTilePrefab), EClauseSign::Outside, { m_id })
	//		}));

	//		solver->makeGraphConstraint<ClauseConstraint>(grid, ENoGood::NoGood, GraphCulledVector<GraphRelationClause>::allOptional({
	//			GraphRelationClause(selfTilePrefab, { m_id }),
	//			GraphRelationClause(selfTilePrefabPos, { pos + 1 }),
	//			GraphRelationClause(horizontalShift->map(verticalShift)->map(selfTilePrefabPos), EClauseSign::Outside, { pos })
	//		}));
	//	}
	//	
	//	// Next
	//	if (pos < m_positions.size() - 1)
	//	{
	//		Position nextLoc = m_positions[pos + 1];
	//		int diffX = currLoc.x - nextLoc.x;
	//		int diffY = currLoc.y - nextLoc.y;
	//		auto horizontalShift = make_shared<TopologyLinkIndexGraphRelation>(ITopology::adapt(grid), (diffY >= 0 ? PlanarGridTopology::moveLeft(diffY) : PlanarGridTopology::moveRight(-diffY)));
	//		auto verticalShift = make_shared<TopologyLinkIndexGraphRelation>(ITopology::adapt(grid), (diffX >= 0 ? PlanarGridTopology::moveUp(diffX) : PlanarGridTopology::moveDown(-diffX)));

	//		solver->makeGraphConstraint<ClauseConstraint>(grid, ENoGood::NoGood, GraphCulledVector<GraphRelationClause>::allOptional({
	//				GraphRelationClause(selfTilePrefab, { m_id }),
	//				GraphRelationClause(selfTilePrefabPos, { pos + 1 }),
	//				GraphRelationClause(horizontalShift->map(verticalShift)->map(selfTilePrefab), EClauseSign::Outside, { m_id })
	//		}));

	//		solver->makeGraphConstraint<ClauseConstraint>(grid, ENoGood::NoGood, GraphCulledVector<GraphRelationClause>::allOptional({
	//			GraphRelationClause(selfTilePrefab, { m_id }),
	//			GraphRelationClause(selfTilePrefabPos, { pos + 1 }),
	//			GraphRelationClause(horizontalShift->map(verticalShift)->map(selfTilePrefabPos), EClauseSign::Outside, { pos + 2 })
	//		}));
	//	}
	// }
}

const Position& Prefab::getPositionForIndex(int index)
{
	vxy_assert(index >= 0 && index < m_positions.size());
	return m_positions[index];
}

int Prefab::getNumTiles()
{
	return m_positions.size();
}

int Prefab::getTileValAtPos(int x, int y)
{
	vxy_assert(x >= 0 && x < m_tiles.size());
	vxy_assert(y >= 0 && y < m_tiles[x].size());
	return m_tiles[x][y].id();
}

void Prefab::rotate(int times)
{
	for (int t = 0; t < times; t++)
	{
		transpose();
		reverse();
		for (int i = 0; i < m_tiles.size(); i++)
		{
			for (int j = 0; j < m_tiles[0].size(); j++)
			{
				m_tiles[i][j].rotate();
			}
		}
	}
	
}

void Prefab::reflect()
{
	reverse();
	for (int i = 0; i < m_tiles.size(); i++)
	{
		for (int j = 0; j < m_tiles[0].size(); j++)
		{
			m_tiles[i][j].reflect();
		}
	}
}

void Prefab::transpose()
{
	vector<vector<Tile>> tmp(m_tiles[0].size(), vector<Tile>());
	for (int i = 0; i < m_tiles.size(); i++)
	{
		for (int j = 0; j < m_tiles[0].size(); j++)
		{
			tmp[j].push_back(m_tiles[i][j]);
		}
	}
	m_tiles = tmp;
}

void Prefab::reverse()
{
	for (int i = 0; i < m_tiles.size(); i++)
	{
		for (int j = 0, k = m_tiles[0].size() - 1; j < k; j++, k--)
		{
			swap(m_tiles[i][j], m_tiles[i][k]);
		}
	}
}
