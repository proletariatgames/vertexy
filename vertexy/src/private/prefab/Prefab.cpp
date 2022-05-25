// Copyright Proletariat, Inc. All Rights Reserved.
#include "prefab/Prefab.h"

#include "ConstraintSolver.h"
#include "ConstraintTypes.h"
#include "prefab/PrefabManager.h"
#include "variable/SolverVariableDomain.h"

using namespace Vertexy;

Prefab::Prefab(int inID, const shared_ptr<PrefabManager>& inManager, const vector<vector<int>>& inTiles)
{
	m_id = inID;
	m_manager = inManager;
	m_tiles = inTiles;

	// Ensure that this prefab is created with a manager
	vxy_assert(m_manager != nullptr);

	// Instantiate the positions
	for (int x = 0; x < m_tiles.size(); x++)
	{
		for (int y = 0; y < m_tiles[x].size(); y++)
		{
			// Skip elements that aren't in the prefab
			if (m_tiles[x][y] == INVALID_TILE)
			{
				continue;
			}

			m_positions.push_back(Position{ x,y });
		}
	}
}

void Prefab::generatePrefabConstraints(ConstraintSolver* solver, const shared_ptr<PlanarGridTopology>& grid, const shared_ptr<TTopologyVertexData<VarID>>& tileData)
{
	auto selfTile = make_shared<TVertexToDataGraphRelation<VarID>>(tileData);
	auto selfTilePrefab = make_shared<TVertexToDataGraphRelation<VarID>>(m_manager->getTilePrefabData());
	auto selfTilePrefabPos = make_shared<TVertexToDataGraphRelation<VarID>>(m_manager->getTilePrefabPosData());

	// Ensure we have a real position if we have a prefab
	solver->makeGraphConstraint<ClauseConstraint>(grid, ENoGood::NoGood,
		GraphRelationClause(selfTilePrefab, { m_id }),
		GraphRelationClause(selfTilePrefabPos, { NO_PREFAB_POS })
	);

	// Ensure we don't use invalid values over this prefab's max
	for (int x = m_positions.size() + 1; x <= m_manager->getMaxPrefabSize(); x++)
	{
		solver->makeGraphConstraint<ClauseConstraint>(grid, ENoGood::NoGood,
			GraphRelationClause(selfTilePrefab, { m_id }),
			GraphRelationClause(selfTilePrefabPos, { x })
		);
	}
	
	for (int pos = 0; pos < m_positions.size(); pos++)
	{
		Position currLoc = m_positions[pos];

		// Self
		solver->makeGraphConstraint<ClauseConstraint>(grid, ENoGood::NoGood,
			GraphRelationClause(selfTile, EClauseSign::Outside, { m_tiles[currLoc.x][currLoc.y] }),
			GraphRelationClause(selfTilePrefab, { m_id }),
			GraphRelationClause(selfTilePrefabPos, { pos + 1 })
		);

		// Prev
		if (pos > 0)
		{
			Position prevLoc = m_positions[pos - 1];
			int diffX = currLoc.x - prevLoc.x;
			int diffY = currLoc.y - prevLoc.y;
			auto horizontalShift = make_shared<TopologyLinkIndexGraphRelation>(ITopology::adapt(grid), (diffY >= 0 ? PlanarGridTopology::moveLeft(diffY) : PlanarGridTopology::moveRight(-diffY)));
			auto verticalShift = make_shared<TopologyLinkIndexGraphRelation>(ITopology::adapt(grid), (diffX >= 0 ? PlanarGridTopology::moveUp(diffX) : PlanarGridTopology::moveDown(-diffX)));

			solver->makeGraphConstraint<ClauseConstraint, EOutOfBoundsMode::DropArrayArgument>(grid, ENoGood::NoGood, vector{
					GraphRelationClause(selfTilePrefab, { m_id }),
					GraphRelationClause(selfTilePrefabPos, { pos + 1 }),
					GraphRelationClause(horizontalShift->map(verticalShift)->map(selfTilePrefab), EClauseSign::Outside, { m_id })
			});

			solver->makeGraphConstraint<ClauseConstraint, EOutOfBoundsMode::DropArrayArgument>(grid, ENoGood::NoGood, vector{
				GraphRelationClause(selfTilePrefab, { m_id }),
				GraphRelationClause(selfTilePrefabPos, { pos + 1 }),
				GraphRelationClause(horizontalShift->map(verticalShift)->map(selfTilePrefabPos), EClauseSign::Outside, { pos })
			});
		}
		
		// Next
		if (pos < m_positions.size() - 1)
		{
			Position nextLoc = m_positions[pos + 1];
			int diffX = currLoc.x - nextLoc.x;
			int diffY = currLoc.y - nextLoc.y;
			auto horizontalShift = make_shared<TopologyLinkIndexGraphRelation>(ITopology::adapt(grid), (diffY >= 0 ? PlanarGridTopology::moveLeft(diffY) : PlanarGridTopology::moveRight(-diffY)));
			auto verticalShift = make_shared<TopologyLinkIndexGraphRelation>(ITopology::adapt(grid), (diffX >= 0 ? PlanarGridTopology::moveUp(diffX) : PlanarGridTopology::moveDown(-diffX)));

			solver->makeGraphConstraint<ClauseConstraint, EOutOfBoundsMode::DropArrayArgument>(grid, ENoGood::NoGood, vector{
					GraphRelationClause(selfTilePrefab, { m_id }),
					GraphRelationClause(selfTilePrefabPos, { pos + 1 }),
					GraphRelationClause(horizontalShift->map(verticalShift)->map(selfTilePrefab), EClauseSign::Outside, { m_id })
			});

			solver->makeGraphConstraint<ClauseConstraint, EOutOfBoundsMode::DropArrayArgument>(grid, ENoGood::NoGood, vector{
				GraphRelationClause(selfTilePrefab, { m_id }),
				GraphRelationClause(selfTilePrefabPos, { pos + 1 }),
				GraphRelationClause(horizontalShift->map(verticalShift)->map(selfTilePrefabPos), EClauseSign::Outside, { pos + 2 })
			});
		}
	}
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
	return m_tiles[x][y];
}