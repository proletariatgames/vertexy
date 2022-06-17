// Copyright Proletariat, Inc. All Rights Reserved.

#include "TileTests.h"
#include "prefab/TileSolver.h"

#include "ConstraintSolver.h"
#include "util/SolverDecisionLog.h"

#include "prefab/Prefab.h"
#include "prefab/Tile.h"

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

int TileTests::solveImplicit(int times, int seed, bool printVerbose)
{
	int nErrorCount = 0;
	/*string input = R"({
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
	})";*/

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

	ConstraintSolver solver(TEXT("PatternTest"), seed);
	TileSolver tilingSolver(&solver, 20, 20, 3, true, true);
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
	auto debugStrat = make_shared<DebugStrategy>(solver, tilingSolver);
	solver.addDecisionHeuristic(debugStrat);

	solver.solve();
	solver.dumpStats(printVerbose);

	// EATEST_VERIFY(solver.getCurrentStatus() == EConstraintSolverResult::Solved);
	if (printVerbose)
	{
		print(solver, tilingSolver);
		tilingSolver.exportJson("test.json");
	}
	if (outputLog != nullptr)
	{
		outputLog->write(TEXT("TileSet.txt"));
		outputLog->writeBreadcrumbs(solver, TEXT("TileSetDecisionLog.txt"));
	}

	return nErrorCount;
}

int TileTests::check()
{
	int nErrorCount = 0;
	return nErrorCount;
}

void TileTests::print(ConstraintSolver& solver, TileSolver& tileSolver)
{
	int numCols = tileSolver.grid()->GetWidth();
	int numRows = tileSolver.grid()->GetHeight();

	for (int y = 0; y < numRows; ++y)
	{
		wstring out;
		for (int x = 0; x < numCols; ++x)
		{
			int node = tileSolver.grid()->coordinateToIndex(x, y);
			int prefabID = solver.getSolvedValue(tileSolver.tileData()->getData()[node]);
			out.append_sprintf(TEXT("[%d]"), tileSolver.prefabs()[prefabID-1]->tiles()[0][0].id());
		}
		VERTEXY_LOG("%s", out.c_str());
	}
	VERTEXY_LOG("");

}

