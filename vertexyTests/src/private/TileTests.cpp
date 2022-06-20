// Copyright Proletariat, Inc. All Rights Reserved.
#include "TileTests.h"
#include "ConstraintSolver.h"
#include "EATest/EATest.h"
#include "prefab/TileSolver.h"
#include "prefab/Prefab.h"
#include "prefab/Tile.h"
#include "util/SolverDecisionLog.h"

using namespace VertexyTests;

static constexpr bool WRITE_BREADCRUMB_LOG = false;

class DebugStrategy : public ISolverDecisionHeuristic
{
public:
	DebugStrategy(ConstraintSolver& solver, TileSolver& tileSolver)
		: m_tileSolver(tileSolver)
		, m_solver(solver)
	{
	}

	virtual bool getNextDecision(SolverDecisionLevel level, VarID& var, ValueSet& chosenValues) override
	{
		return false;
	}

	virtual void onVariableAssignment(VarID var, const ValueSet& prevValues, const ValueSet& newValues)
	{
		if (m_it % 100 == 0)
		{
			m_tileSolver.exportJson(to_string(m_count++) + ".json");
		}
		m_it++;
	}

protected:
	int m_it = 0;
	int m_count = 0;
	TileSolver& m_tileSolver;
	ConstraintSolver& m_solver;
};

int TileTests::solve(int times, int seed, string input, bool allowRotation, bool allowReflection, bool printVerbose /*= true*/)
{
	int nErrorCount = 0;
	ConstraintSolver solver(TEXT("PatternTest"), seed);
	TileSolver tilingSolver(&solver, 10, 10, 2, false, false);
	tilingSolver.parseJsonString(input);
	//tilingSolver.parseJsonFile("W:\\Projetos\\Meus\\C++\\misc\\input.json");

	shared_ptr<SolverDecisionLog> outputLog;
	if constexpr (WRITE_BREADCRUMB_LOG)
	{
		outputLog = make_shared<SolverDecisionLog>();
	}

	if (outputLog != nullptr)
	{
		solver.setOutputLog(outputLog);
	}

	// DEBUG
	// auto debugStrat = make_shared<DebugStrategy>(solver, tilingSolver);
	// solver.addDecisionHeuristic(debugStrat);

	for (int i = 0; i < times; ++i)
	{
		solver.solve();
		solver.dumpStats(printVerbose);

		EATEST_VERIFY(solver.getCurrentStatus() == EConstraintSolverResult::Solved);
		if (printVerbose)
		{
			print(solver, tilingSolver);
		}
	}
	if (outputLog != nullptr)
	{
		outputLog->write(TEXT("TileTest.txt"));
		outputLog->writeBreadcrumbs(solver, TEXT("TileTestDecisionLog.txt"));
		tilingSolver.exportJson("TileTest.json");
	}
	nErrorCount += check(solver, tilingSolver);
	return nErrorCount;
}

int TileTests::solveBasic(int times, int seed, bool printVerbose)
{
	string input = R"({
		"tile_size": 10,
		"tiles":[
		{
			"id": 0,
			"name": "(255,255,255) ",
			"symmetry": "X"
		},
		{
			"id": 1,
			"name": "(0,0,0) ",
			"symmetry": "X"
		},
		{
			"id": 2,
			"name": "(255,0,0) ",
			"symmetry": "X"
		}
		],
		"grid":[
			[0,0,0,0],
			[0,1,1,1],
			[0,1,2,1],
			[0,1,1,1]
		],
		"config":[
			[0,0,0,0],
			[0,0,0,0],
			[0,0,0,0],
			[0,0,0,0]
		]
	})";
	return solve(times, seed, input, false, false, printVerbose);
}

int TileTests::solveRotationReflection(int times, int seed, bool printVerbose /*= true*/)
{
	string input = R"({
		"tile_size": 10,
		"tiles":[
		{
			"id": 0,
			"name": "(255,255,255) ",
			"symmetry": "X"
		},
		{
			"id": 1,
			"name": "(0,0,0) ",
			"symmetry": "X"
		}
		],
		"grid":[
			[0,0,0,0,0,0,0,1],
			[1,0,0,0,0,0,0,0],
			[0,1,1,1,0,0,0,0],
			[0,0,0,0,1,1,0,0],
			[0,0,0,0,0,0,1,1],
			[1,1,1,0,0,0,0,0],
			[0,0,0,1,1,0,0,0],
			[0,0,0,0,0,1,1,0]
		],
		"config":[
			[0,0,0,0,0,0,0,0],
			[0,0,0,0,0,0,0,0],
			[0,0,0,0,0,0,0,0],
			[0,0,0,0,0,0,0,0],
			[0,0,0,0,0,0,0,0],
			[0,0,0,0,0,0,0,0],
			[0,0,0,0,0,0,0,0],
			[0,0,0,0,0,0,0,0]
		]
		})";
	return solve(times, seed, input, true, true, printVerbose);
}


int TileTests::check(ConstraintSolver& solver, TileSolver& tileSolver)
{
	int nErrorCount = 0;
	int numCols = tileSolver.grid()->getWidth();
	int numRows = tileSolver.grid()->getHeight();
	int kernelSize = tileSolver.kernelSize();

	for (int y = 0; y < numRows; y++)
	{
		for (int x = 0; x < numCols; x++)
		{
			int node = tileSolver.grid()->coordinateToIndex(x, y);
			int prefabId = solver.getSolvedValue(tileSolver.tileData()->getData()[node]);
			const auto& prefab = tileSolver.prefabs()[prefabId - 1];
			for (int i = 0; i < kernelSize; ++i)
			{
				for (int j = 0; j < kernelSize; ++j)
				{
					if (y + i >= numRows || x + j >= numCols)
					{
						continue;
					}
					int offsetNode = tileSolver.grid()->coordinateToIndex(x + j, y + i);
					int offserPrefabId = solver.getSolvedValue(tileSolver.tileData()->getData()[offsetNode]);
					if (prefab->tiles()[i][j].id() != tileSolver.prefabs()[offserPrefabId - 1]->tiles()[0][0].id() ||
						prefab->tiles()[i][j].configuration() != tileSolver.prefabs()[offserPrefabId - 1]->tiles()[0][0].configuration())
					{
						nErrorCount++;
					}
				}
			}
		}
	}
	return nErrorCount;
}

void TileTests::print(ConstraintSolver& solver, TileSolver& tileSolver)
{
	int numCols = tileSolver.grid()->getWidth();
	int numRows = tileSolver.grid()->getHeight();

	for (int y = 0; y < numRows; y++)
	{
		wstring out;
		for (int x = 0; x < numCols; x++)
		{
			int node = tileSolver.grid()->coordinateToIndex(x, y);
			int prefabID = solver.getSolvedValue(tileSolver.tileData()->getData()[node]);
			out.append_sprintf(TEXT("[%d]"), tileSolver.prefabs()[prefabID - 1]->tiles()[0][0].id());
		}
		VERTEXY_LOG("%s", out.c_str());
	}
	VERTEXY_LOG("");
}