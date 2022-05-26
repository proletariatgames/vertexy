// Copyright Proletariat, Inc. All Rights Reserved.
#include "TileSet.h"
#include "tileset/TileSolver.h"
#include "ConstraintSolver.h"
#include "util/SolverDecisionLog.h"
#include "EATest/EATest.h"

using namespace VertexyTests;

// Whether to write a decision log as DecisionLog.txt
static constexpr bool WRITE_BREADCRUMB_LOG = false;

int TileSet::solve(int times, int numRows, int numCols, int seed, bool printVerbose)
{
    int nErrorCount = 0;

	ConstraintSolver solver(TEXT("TileSolver"), seed);
    VERTEXY_LOG("TestMaze(%d)", solver.getSeed());

	string input = R"({
        "tile_size": 10,
		"projection": "isometric",
        "tiles":[
            {
                "name": "empty",
                "symmetry": "X"
            },
            {
                "name": "line",
                "symmetry": "I",
                "weight_min": 0.1,
                "weight_max": 0.9
            },
			{
				"name": "corner",
				"symmetry": "L",
				"weight_min": 0.1,
				"weight_max": 0.9
			}
        ],
        "relations":[
            { "self": "empty", "right": "empty" },
			{ "self": "empty","down": "line" },
			{ "self": "empty","right": "corner" },
			{ "self": "line","right": "line" },
			{ "self": "line","down": "line" },
			{ "self": "corner","right": "line" },
			{ "self": "corner","down": "line" }
        ]
    })";

    TileSolver tileSolver(&solver, numRows, numCols);
    tileSolver.parseJsonString(input);

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

	// EATEST_VERIFY(solver.getCurrentStatus() == EConstraintSolverResult::Solved);
	if (printVerbose)
	{
		print(solver, tileSolver);
		tileSolver.exportResults();
	}
	if (outputLog != nullptr)
	{
		outputLog->write(TEXT("TileSet.txt"));
		outputLog->writeBreadcrumbs(solver, TEXT("TileSetDecisionLog.txt"));
	}

	return 0;
}

int TileSet::check(ConstraintSolver* solver, const vector<VarID>& vars)
{
	return 0;
}

void TileSet::print(ConstraintSolver& solver, TileSolver& tileSolver)
{
	int numCols = tileSolver.grid()->GetWidth();
	int numRows = tileSolver.grid()->GetHeight();

	for (const auto& data : { tileSolver.tileData(), tileSolver.configData() })
	{	
		for (int y = 0; y < numRows; ++y)
		{
			wstring out;
			for (int x = 0; x < numCols; ++x)
			{
				int node = tileSolver.grid()->coordinateToIndex(x, y);
				out.append_sprintf(TEXT("%d"), solver.getSolvedValue(data->getData()[node]));
			}
			VERTEXY_LOG("%s", out.c_str());
		}		
		VERTEXY_LOG("");
	}
	
}

