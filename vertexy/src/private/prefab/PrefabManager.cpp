// Copyright Proletariat, Inc. All Rights Reserved.
#include "prefab/PrefabManager.h"

#include "ConstraintSolver.h"
#include "ConstraintTypes.h"
#include "prefab/Prefab.h"
#include "variable/SolverVariableDomain.h"
#include "prefab/Tile.h"

using namespace Vertexy;

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

void PrefabManager::createPrefab(const vector<vector<Tile>>& inTiles, bool allowRotation, bool allowReflection)
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

	// if we allow rotation and/or reflection, create prefab configurations
	if (!allowRotation && !allowReflection)
		return;
	
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

			// Self
			m_solver->makeGraphConstraint<ClauseConstraint>(m_grid, ENoGood::NoGood,
				GraphRelationClause(selfTile, EClauseSign::Outside, { prefab->tiles()[currLoc.x][currLoc.y].id() }),
				GraphRelationClause(selfTilePrefab, { prefab->id() }),
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
					GraphRelationClause(selfTilePrefab, { prefab->id() }),
					GraphRelationClause(selfTilePrefabPos, { pos + 1 }),
					GraphRelationClause(horizontalShift->map(verticalShift)->map(selfTilePrefab), EClauseSign::Outside, { prefab->id() })
				}));

				m_solver->makeGraphConstraint<ClauseConstraint>(m_grid, ENoGood::NoGood, GraphCulledVector<GraphRelationClause>::allOptional({
					GraphRelationClause(selfTilePrefab, { prefab->id() }),
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
					GraphRelationClause(selfTilePrefab, { prefab->id() }),
					GraphRelationClause(selfTilePrefabPos, { pos + 1 }),
					GraphRelationClause(horizontalShift->map(verticalShift)->map(selfTilePrefab), EClauseSign::Outside, { prefab->id() })
				}));

				m_solver->makeGraphConstraint<ClauseConstraint>(m_grid, ENoGood::NoGood, GraphCulledVector<GraphRelationClause>::allOptional({
					GraphRelationClause(selfTilePrefab, { prefab->id() }),
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

int PrefabManager::getMaxPrefabSize()
{
	return m_maxPrefabSize;
}

void PrefabManager::createDefaultTestPrefab(int index, bool rot, bool refl)
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
		}, rot, refl);
		break;

	case 1:
		createPrefab({
			{ t1, tx, t1 },
			{ tx, tx, tx },
			{ t1, tx, tx }
		}, rot, refl);
		break;

	default: vxy_assert(false);
	}
}