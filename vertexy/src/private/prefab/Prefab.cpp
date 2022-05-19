// Copyright Proletariat, Inc. All Rights Reserved.
#include "prefab/Prefab.h"

#include "ConstraintSolver.h"
#include "ConstraintTypes.h"
#include "prefab/PrefabManager.h"
#include "variable/SolverVariableDomain.h"

using namespace Vertexy;

Prefab::Prefab(int inID, PrefabManager* inManager, vector<vector<int>> inTiles)
{
	id = inID;
	manager = inManager;
	tiles = inTiles;

	// Instantiate the positions
	for (int x = 0; x < tiles.size(); x++)
	{
		for (int y = 0; y < tiles[x].size(); y++)
		{
			// Skip elements that aren't in the prefab
			if (tiles[x][y] == INVALID_TILE)
			{
				continue;
			}

			positions.push_back(vector{ x,y });
		}
	}
}

void Prefab::GeneratePrefabConstraints(ConstraintSolver& solver, shared_ptr<PlanarGridTopology> grid, shared_ptr<TTopologyVertexData<VarID>> tileData)
{
	auto selfTile = make_shared<TVertexToDataGraphRelation<VarID>>(tileData);
	auto selfTilePrefab = make_shared<TVertexToDataGraphRelation<VarID>>(manager->getTilePrefabData());
	auto selfTilePrefabPos = make_shared<TVertexToDataGraphRelation<VarID>>(manager->getTilePrefabPosData());

	// Ensure we have a real position if we have a prefab
	solver.makeGraphConstraint<ClauseConstraint>(grid, ENoGood::NoGood,
		GraphRelationClause(selfTilePrefab, { id }),
		GraphRelationClause(selfTilePrefabPos, { NO_PREFAB_POS })
		);

	// Ensure we don't use invalid values over this prefab's max
	for (int x = positions.size() + 1; x <= manager->getMaxPrefabSize(); x++)
	{
		solver.makeGraphConstraint<ClauseConstraint>(grid, ENoGood::NoGood,
			GraphRelationClause(selfTilePrefab, { id }),
			GraphRelationClause(selfTilePrefabPos, { x })
			);
	}
	
	for (int pos = 0; pos < positions.size(); pos++)
	{
		vector<int> currLoc = positions[pos];

		// Self
		solver.makeGraphConstraint<ClauseConstraint>(grid, ENoGood::NoGood,
			GraphRelationClause(selfTile, EClauseSign::Outside, { tiles[currLoc[0]][currLoc[1]] }),
			GraphRelationClause(selfTilePrefab, { id }),
			GraphRelationClause(selfTilePrefabPos, { pos + 1 })
			);

		// Prev
		if (pos > 0)
		{
			vector<int> prevLoc = positions[pos - 1];
			int diffX = currLoc[0] - prevLoc[0];
			int diffY = currLoc[1] - prevLoc[1];
			auto horizontalShift = make_shared<TopologyLinkIndexGraphRelation>(ITopology::adapt(grid), (diffY >= 0 ? PlanarGridTopology::moveLeft(diffY) : PlanarGridTopology::moveRight(-diffY)));
			auto verticalShift = make_shared<TopologyLinkIndexGraphRelation>(ITopology::adapt(grid), (diffX >= 0 ? PlanarGridTopology::moveUp(diffX) : PlanarGridTopology::moveDown(-diffX)));

			solver.makeGraphConstraint<ClauseConstraint, EOutOfBoundsMode::DropArrayArgument>(grid, ENoGood::NoGood, vector{
					GraphRelationClause(selfTilePrefab, { id }),
					GraphRelationClause(selfTilePrefabPos, { pos + 1 }),
					GraphRelationClause(horizontalShift->map(verticalShift)->map(selfTilePrefab), EClauseSign::Outside, { id })
				});

			solver.makeGraphConstraint<ClauseConstraint, EOutOfBoundsMode::DropArrayArgument>(grid, ENoGood::NoGood, vector{
				GraphRelationClause(selfTilePrefab, { id }),
				GraphRelationClause(selfTilePrefabPos, { pos + 1 }),
				GraphRelationClause(horizontalShift->map(verticalShift)->map(selfTilePrefabPos), EClauseSign::Outside, { pos })
				});
		}
		
		// Next
		if (pos < positions.size() - 1)
		{
			vector<int> nextLoc = positions[pos + 1];
			int diffX = currLoc[0] - nextLoc[0];
			int diffY = currLoc[1] - nextLoc[1];
			auto horizontalShift = make_shared<TopologyLinkIndexGraphRelation>(ITopology::adapt(grid), (diffY >= 0 ? PlanarGridTopology::moveLeft(diffY) : PlanarGridTopology::moveRight(-diffY)));
			auto verticalShift = make_shared<TopologyLinkIndexGraphRelation>(ITopology::adapt(grid), (diffX >= 0 ? PlanarGridTopology::moveUp(diffX) : PlanarGridTopology::moveDown(-diffX)));

			solver.makeGraphConstraint<ClauseConstraint, EOutOfBoundsMode::DropArrayArgument>(grid, ENoGood::NoGood, vector{
					GraphRelationClause(selfTilePrefab, { id }),
					GraphRelationClause(selfTilePrefabPos, { pos + 1 }),
					GraphRelationClause(horizontalShift->map(verticalShift)->map(selfTilePrefab), EClauseSign::Outside, { id })
				});

			solver.makeGraphConstraint<ClauseConstraint, EOutOfBoundsMode::DropArrayArgument>(grid, ENoGood::NoGood, vector{
				GraphRelationClause(selfTilePrefab, { id }),
				GraphRelationClause(selfTilePrefabPos, { pos + 1 }),
				GraphRelationClause(horizontalShift->map(verticalShift)->map(selfTilePrefabPos), EClauseSign::Outside, { pos + 2 })
				});
		}
	}
}

vector<int> Prefab::getPositionForIndex(int index)
{
	vxy_assert(index >= 0 && index < positions.size());
	return positions[index];
}

int Prefab::getNumTiles()
{
	return positions.size();
}

int Prefab::getTileValAtPos(int x, int y)
{
	vxy_assert(x >= 0 && x < tiles.size());
	vxy_assert(y >= 0 && y < tiles[x].size());
	return tiles[x][y];
}