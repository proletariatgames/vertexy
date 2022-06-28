// Copyright Proletariat, Inc. All Rights Reserved.
#include "PrefabTest.h"

#include "ConstraintSolver.h"
#include "EATest/EATest.h"
#include "prefab/Prefab.h"
#include "util/SolverDecisionLog.h"
#include "variable/SolverVariableDomain.h"

using namespace VertexyTests;

// Whether to write a decision log as DecisionLog.txt
static constexpr bool WRITE_BREADCRUMB_LOG = true;

static constexpr bool PRINT_TILE_VALS = true;
static constexpr bool PRINT_PREFAB_IDS = true;
static constexpr bool PRINT_PREFAB_POS = true;

int PrefabTestSolver::solveBasic(int times, int seed, bool printVerbose)
{
	int nErrorCount = 0;

	//
	// Each tile of the map takes is one of these values:
	//
	constexpr int BLANK_IDX = 0;
	constexpr int WALL_IDX = 1;

	int numRows = 3;
	int numCols = 3;

	for (int time = 0; time < times; ++time)
	{
		ConstraintSolver solver(TEXT("PrefabTest-Basic"), seed);

		// Create the topology for the grid
		shared_ptr<PlanarGridTopology> grid = make_shared<PlanarGridTopology>(numCols, numRows);

		// Create the PrefabManager
		shared_ptr<PrefabManager> prefabManager = PrefabManager::create(&solver, grid);

		// Generate test prefabs
		prefabManager->createDefaultTestPrefab(0);
		prefabManager->createDefaultTestPrefab(1);

		// The domains for the various types of variables
		SolverVariableDomain tileDomain(0, 1);

		// Create a variable for each tile in the grid
		auto tileData = solver.makeVariableGraph(TEXT("TileVars"), ITopology::adapt(grid), tileDomain, TEXT("Tile"));

		// Generate the prefab constraints
		prefabManager->generatePrefabConstraints(tileData);

		// Set some initial values
		solver.setInitialValues(prefabManager->getTilePrefabData()->getData()[0], { 2 });
		solver.setInitialValues(prefabManager->getTilePrefabData()->getData()[4], { 1 });

		shared_ptr<SolverDecisionLog> outputLog;
		if constexpr (WRITE_BREADCRUMB_LOG)
		{
			outputLog = make_shared<SolverDecisionLog>();
		}

		if (outputLog != nullptr)
		{
			solver.setOutputLog(outputLog);
		}

		solver.solve();
		solver.dumpStats(printVerbose);

		EATEST_VERIFY(solver.getCurrentStatus() == EConstraintSolverResult::Solved);
		if (printVerbose)
		{
			print(&solver, grid, tileData, prefabManager);
		}

		if (outputLog != nullptr)
		{
			outputLog->write(TEXT("PrefabTest-Basic-Output.txt"));
		}

		nErrorCount += check(&solver, tileData, prefabManager);
	}

	return nErrorCount;
}

int PrefabTestSolver::solveJson(int times, int seed, bool printVerbose)
{
	int nErrorCount = 0;

	//
	// Each tile of the map takes is one of these values:
	//
	constexpr int BLANK_IDX = 0;
	constexpr int WALL_IDX = 1;

	int numRows = 3;
	int numCols = 3;

	for (int time = 0; time < times; ++time)
	{
		ConstraintSolver solver(TEXT("PrefabTest-Json"), seed);

		// Create the topology for the grid
		shared_ptr<PlanarGridTopology> grid = make_shared<PlanarGridTopology>(numCols, numRows);

		// Create the PrefabManager
		shared_ptr<PrefabManager> prefabManager = PrefabManager::create(&solver, grid);

		// Generate test prefabs
		prefabManager->createPrefabFromJson(TEXT("../../prefabs/test1.json"));
		prefabManager->createPrefabFromJson(TEXT("../../prefabs/test2.json"));

		// The domains for the various types of variables
		SolverVariableDomain tileDomain(0, 1);

		// Create a variable for each tile in the grid
		auto tileData = solver.makeVariableGraph(TEXT("TileVars"), ITopology::adapt(grid), tileDomain, TEXT("Tile"));

		// Generate the prefab constraints
		prefabManager->generatePrefabConstraints(tileData);

		// Set some initial values
		solver.setInitialValues(prefabManager->getTilePrefabData()->getData()[4], { 1 });
		solver.setInitialValues(prefabManager->getTilePrefabData()->getData()[0], prefabManager->getPrefabIdsByName(TEXT("test2")));

		shared_ptr<SolverDecisionLog> outputLog;
		if constexpr (WRITE_BREADCRUMB_LOG)
		{
			outputLog = make_shared<SolverDecisionLog>();
		}

		if (outputLog != nullptr)
		{
			solver.setOutputLog(outputLog);
		}

		solver.solve();
		solver.dumpStats(printVerbose);

		EATEST_VERIFY(solver.getCurrentStatus() == EConstraintSolverResult::Solved);
		if (printVerbose)
		{
			print(&solver, grid, tileData, prefabManager);
		}

		if (outputLog != nullptr)
		{
			outputLog->write(TEXT("PrefabTest-Json-Output.txt"));
		}

		nErrorCount += check(&solver, tileData, prefabManager);
	}

	return nErrorCount;
}

int PrefabTestSolver::solveNeighbor(int times, int seed, bool printVerbose)
{
	int nErrorCount = 0;

	//
	// Each tile of the map takes is one of these values:
	//
	constexpr int BLANK_IDX = 0;
	constexpr int WALL_IDX = 1;

	int numRows = 5;
	int numCols = 6;

	for (int time = 0; time < times; ++time)
	{
		ConstraintSolver solver(TEXT("PrefabTest-Neighbor"), seed);

		// Create the topology for the grid
		shared_ptr<PlanarGridTopology> grid = make_shared<PlanarGridTopology>(numCols, numRows);

		// Create the PrefabManager
		shared_ptr<PrefabManager> prefabManager = PrefabManager::create(&solver, grid);

		// Generate test prefabs
		prefabManager->createPrefabFromJson(TEXT("../../prefabs/test3.json"));
		prefabManager->createPrefabFromJson(TEXT("../../prefabs/test4.json"));

		// The domains for the various types of variables
		SolverVariableDomain tileDomain(0, 1);

		// Create a variable for each tile in the grid
		auto tileData = solver.makeVariableGraph(TEXT("TileVars"), ITopology::adapt(grid), tileDomain, TEXT("Tile"));

		// Generate the prefab constraints
		prefabManager->generatePrefabConstraints(tileData);

		// Set some initial values to ensure the test3 prefab is in the middle of the grid, allowing space for neighbors
		solver.setInitialValues(prefabManager->getTilePrefabData()->getData()[8], prefabManager->getPrefabIdsByName(TEXT("test3")));
		solver.setInitialValues(prefabManager->getTilePrefabData()->getData()[20], prefabManager->getPrefabIdsByName(TEXT("test3")));

		shared_ptr<SolverDecisionLog> outputLog;
		if constexpr (WRITE_BREADCRUMB_LOG)
		{
			outputLog = make_shared<SolverDecisionLog>();
		}

		if (outputLog != nullptr)
		{
			solver.setOutputLog(outputLog);
		}

		solver.solve();
		solver.dumpStats(printVerbose);

		EATEST_VERIFY(solver.getCurrentStatus() == EConstraintSolverResult::Solved);
		if (printVerbose)
		{
			print(&solver, grid, tileData, prefabManager);
		}

		if (outputLog != nullptr)
		{
			outputLog->write(TEXT("PrefabTest-Neighbor-Output.txt"));
		}

		nErrorCount += checkNeighbor(&solver, tileData, prefabManager);
	}

	return nErrorCount;
}

int PrefabTestSolver::solveRotationReflection(int times, int seed, bool printVerbose /*= true*/)
{
	int nErrorCount = 0;

	int numRows = 3;
	int numCols = 3;

	for (int time = 0; time < times; ++time)
	{
		ConstraintSolver solver(TEXT("PrefabTest-Rot/Refl"), seed);

		// Create the topology for the grid
		shared_ptr<PlanarGridTopology> grid = make_shared<PlanarGridTopology>(numCols, numRows);

		// Create the PrefabManager
		shared_ptr<PrefabManager> prefabManager = PrefabManager::create(&solver, grid);

		// Generate test prefabs (with rotation and reflection)
		prefabManager->createDefaultTestPrefab(0, TEXT("test1"), true, true);
		prefabManager->createDefaultTestPrefab(1, TEXT("test2"), true, true);

		// The domains for the various types of variables
		SolverVariableDomain tileDomain(0, 1);

		// Create a variable for each tile in the grid
		auto tileData = solver.makeVariableGraph(TEXT("TileVars"), ITopology::adapt(grid), tileDomain, TEXT("Tile"));

		// Generate the prefab constraints
		prefabManager->generatePrefabConstraints(tileData);

		// Set some initial values (allows any rotation/reflection for both prefabs).
		// First prefab can have 8 configurations (1 to 8).
		// Second prefab can have 8 configurations (9 to 16).
		solver.setInitialValues(prefabManager->getTilePrefabData()->getData()[8], prefabManager->getPrefabIdsByName(TEXT("test2")));
		solver.setInitialValues(prefabManager->getTilePrefabData()->getData()[4], prefabManager->getPrefabIdsByName(TEXT("test1")));

		shared_ptr<SolverDecisionLog> outputLog;
		if constexpr (WRITE_BREADCRUMB_LOG)
		{
			outputLog = make_shared<SolverDecisionLog>();
		}

		if (outputLog != nullptr)
		{
			solver.setOutputLog(outputLog);
		}

		solver.solve();
		solver.dumpStats(printVerbose);

		EATEST_VERIFY(solver.getCurrentStatus() == EConstraintSolverResult::Solved);
		if (printVerbose)
		{
			print(&solver, grid, tileData, prefabManager);
		}

		if (outputLog != nullptr)
		{
			outputLog->write(TEXT("PrefabTest-RotRefl-Output.txt"));
		}

		nErrorCount += check(&solver, tileData, prefabManager);
	}

	return nErrorCount;
}

void PrefabTestSolver::print(ConstraintSolver* solver, shared_ptr<PlanarGridTopology> grid, shared_ptr<TTopologyVertexData<VarID>> tileData, const shared_ptr<PrefabManager>& prefabManager)
{
	vector<shared_ptr<TTopologyVertexData<VarID>>> graphVars = { tileData, prefabManager->getTilePrefabData(), prefabManager->getTilePrefabPosData() };
	
	for (int varIndex = 0; varIndex < graphVars.size(); varIndex++)
	{
		// Only print specified graphs
		switch (varIndex)
		{
			case 0: if (!PRINT_TILE_VALS) { continue; } break;
			case 1: if (!PRINT_PREFAB_IDS) { continue; } break;
			case 2: if (!PRINT_PREFAB_POS) { continue; } break;
		}

		// Print out the graph
		for (int row = 0; row < grid->getHeight(); row++)
		{
			wstring out = TEXT("");

			for (int col = 0; col < grid->getWidth(); col++)
			{
				out.append_sprintf(TEXT("[%d] "), solver->getSolvedValue(graphVars[varIndex]->getData()[row * grid->getWidth() + col]));
			}

			VERTEXY_LOG("%s", out.c_str());
		}
		VERTEXY_LOG("");
	}
}

// Pass in a row, column, or square to ensure every valid value is represented exactly once
int PrefabTestSolver::check(ConstraintSolver* solver, shared_ptr<TTopologyVertexData<VarID>> tileData, const shared_ptr<PrefabManager>& prefabManager)
{
	int nErrorCount = 0;

	// Create a PrefabPos counter for each unique prefab
	// These will be incremented in traversal order and are unique starting at 1 for each prefab
	vector<int> prefabPositions;
	for (int x = 0; x < prefabManager->getPrefabs().size(); x++)
	{
		prefabPositions.push_back(Prefab::NO_PREFAB_POS + 1);
	}

	// Iterate over the graph
	for (int x = 0; x < tileData->getData().size(); x++)
	{
		// If this isn't part of a prefab, we don't care about it. Skip it.
		int solvedPrefab = solver->getSolvedValue(prefabManager->getTilePrefabData()->getData()[x]);
		if (solvedPrefab == Prefab::NO_PREFAB_ID)
		{
			continue;
		}
		auto prefab = prefabManager->getPrefabs()[solvedPrefab - 1];
		
		// Check to ensure the PrefabPos is correct
		int solvedPos = solver->getSolvedValue(prefabManager->getTilePrefabPosData()->getData()[x]);
		if (solvedPos != prefabPositions[solvedPrefab - 1])
		{
			nErrorCount++;
		}

		// Check to ensure the TileID matches what the prefab says it is for the given PrefabPos
		int solvedTile = solver->getSolvedValue(tileData->getData()[x]);
		Position tileLoc = prefab->getPositionForIndex(solvedPos - 1);
		int tileVal = prefab->getTileValAtPos(tileLoc.x, tileLoc.y);
		if (solvedTile != tileVal)
		{
			nErrorCount++;
		}

		// Increment the PrefabPos to keep a running track
		prefabPositions[solvedPrefab - 1]++;
	}

	return nErrorCount;
}

// Pass in a row, column, or square to ensure every valid value is represented exactly once
int PrefabTestSolver::checkNeighbor(ConstraintSolver* solver, shared_ptr<TTopologyVertexData<VarID>> tileData, const shared_ptr<PrefabManager>& prefabManager)
{
	bool hasRightNeighbor = false;
	bool hasLeftNeighbor = false;
	bool hasAboveNeighbor = false;
	bool hasBelowNeighbor = false;

	// Iterate over the graph
	for (int x = 0; x < tileData->getData().size(); x++)
	{
		// If this isn't part of prefab 1, we don't care about it. Skip it.
		int solvedPrefab = solver->getSolvedValue(prefabManager->getTilePrefabData()->getData()[x]);
		if (solvedPrefab != 1)
		{
			continue;
		}

		// Whenever we encounter our target prefab, check the right neighbor
		if (solver->getSolvedValue(prefabManager->getTilePrefabData()->getData()[x + 1]) == 2)
		{
			hasRightNeighbor = true;
		}

		if (solver->getSolvedValue(prefabManager->getTilePrefabData()->getData()[x - 1]) == 2)
		{
			hasLeftNeighbor = true;
		}

		if (solver->getSolvedValue(prefabManager->getTilePrefabData()->getData()[x - 6]) == 2)
		{
			hasAboveNeighbor = true;
		}

		if (solver->getSolvedValue(prefabManager->getTilePrefabData()->getData()[x + 6]) == 2)
		{
			hasBelowNeighbor = true;
		}
	}

	return !(hasRightNeighbor && hasLeftNeighbor && hasAboveNeighbor && hasBelowNeighbor);
}