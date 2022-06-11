// Copyright Proletariat, Inc. All Rights Reserved.
#include "Maze.h"
#include "ConstraintSolver.h"
#include "EATest/EATest.h"
#include "constraints/ReachabilityConstraint.h"
#include "constraints/TableConstraint.h"
#include "constraints/IffConstraint.h"
#include "decision/LogOrderHeuristic.h"
#include "program/ProgramDSL.h"
#include "topology/GridTopology.h"
#include "topology/GraphRelations.h"
#include "topology/IPlanarTopology.h"
#include "util/SolverDecisionLog.h"
#include "variable/SolverVariableDomain.h"

using namespace VertexyTests;

// Interval of solver steps to print out current maze status. Set to 1 to see each solver step.
static constexpr int MAZE_REFRESH_RATE = 0;
// The number of keys/doors that should exist in the maze.
static constexpr int NUM_KEYS = 2;
// True to print edge variables in FMaze::Print
static constexpr bool PRINT_EDGES = false;
// Whether to write a decision log as DecisionLog.txt
static constexpr bool WRITE_BREADCRUMB_LOG = false;
// Whether to write a solution file after a solution is found
static constexpr bool WRITE_SOLUTION_FILE = false;
// If >= 0, the step during solving to read and attempt to apply a previously-written solution file.
// Useful for debugging constraints.
static constexpr int ATTEMPT_SOLUTION_AT = -1;

// This implements this decision heuristic interface, but just forwards the decision to the default heuristic.
// Its only purpose is to print out the maze whenever a conflict is detected, for debugging purposes.
class DebugMazeStrategy : public ISolverDecisionHeuristic
{
public:
	DebugMazeStrategy(ConstraintSolver& solver, const shared_ptr<TTopologyVertexData<VarID>>& cells, const shared_ptr<TTopologyVertexData<VarID>>& edges)
		: m_cells(cells)
		, m_edges(edges)
		, m_solver(solver)
	{
	}

	virtual bool getNextDecision(SolverDecisionLevel level, VarID& var, ValueSet& chosenValues) override
	{
		// defer to default
		return false;
	}

	virtual void onClauseLearned() override
	{
		MazeSolver::print(m_cells, m_edges, m_solver);
	}

protected:
	shared_ptr<TTopologyVertexData<VarID>> m_cells;
	shared_ptr<TTopologyVertexData<VarID>> m_edges;
	ConstraintSolver& m_solver;
};

int MazeSolver::solveUsingGraphProgram(int times, int numRows, int numCols, int seed, bool printVerbose)
{
	int nErrorCount = 0;

	auto grid = make_shared<PlanarGridTopology>(numCols, numRows);
	auto edges = make_shared<EdgeTopology>(ITopology::adapt(grid), true, false);
	
	// Define a domain that can be used by formulas. This defines the potential values that a formula can be
	// resolved to.
	VXY_DOMAIN_BEGIN(CellDomain)
		VXY_DOMAIN_VALUE(blank);
		VXY_DOMAIN_VALUE(wall);
		VXY_DOMAIN_VALUE(entrance);
		VXY_DOMAIN_VALUE(exit);
		VXY_DOMAIN_VALUE_ARRAY(keys, NUM_KEYS);
		VXY_DOMAIN_VALUE_ARRAY(doors, NUM_KEYS);
	VXY_DOMAIN_END()

	// Declare a formula called "cellType" of the given domain, with one argument.
	VXY_DOMAIN_FORMULA(cellType, CellDomain, 1);

	// Define masks of values for the cellType formula. 
	const auto M_PASSABLE = cellType.blank|cellType.keys|cellType.entrance|cellType.exit;
	const auto M_IMPASSABLE = cellType.wall|cellType.doors;
	
	// Specify the base program (instantiated only once). Defines base properties of the maze.
	auto baseProgram = Program::define([&](ProgramVertex vertex, int entranceVertex, int exitVertex)
	{
		// define implicit formulas.
		// E.g. up(X, Y) exists for every (X,Y) pair where Y vertex is immediately above X. 
		auto up = Program::graphLink(PlanarGridTopology::moveUp());
		auto down = Program::graphLink(PlanarGridTopology::moveDown());
		auto left = Program::graphLink(PlanarGridTopology::moveLeft());
		auto right = Program::graphLink( PlanarGridTopology::moveRight());
		
		// Define a floating variable. VXY variables don't mean anything outside the context of a rule statement.
		// Within a rule statement, they encode equality. E.g. if "X" shows up in two places in a rule,
		// it means that those Xs are the same. See below.
		VXY_VARIABLE(X);
		
		// define the entrance/exit positions, based on the program inputs.
		cellType(entranceVertex).is(cellType.entrance);
		cellType(exitVertex).is(cellType.exit);
		
		// Define a rule formula border(x,y), which is only true at the edges of the map.
		VXY_FORMULA(border, 1);
		border(vertex) <<= ~Program::hasGraphLink(vertex, PlanarGridTopology::moveUp());
		border(vertex) <<= ~Program::hasGraphLink(vertex, PlanarGridTopology::moveDown());
		border(vertex) <<= ~Program::hasGraphLink(vertex, PlanarGridTopology::moveLeft());
		border(vertex) <<= ~Program::hasGraphLink(vertex, PlanarGridTopology::moveRight());
		
		// Specify types that each cell could be
		cellType(vertex).is(cellType.wall|cellType.blank|cellType.keys|cellType.doors) <<= ~border(vertex);
		
		// border is a wall as long as it's not the entrance or exit.
		cellType(vertex).is(cellType.wall) <<= border(vertex) && ~cellType(vertex).is(cellType.entrance|cellType.exit);
		
		VXY_VARIABLE(RIGHT); VXY_VARIABLE(DOWN);
		VXY_VARIABLE(LEFT); VXY_VARIABLE(UP);
		VXY_VARIABLE(DOWN_RIGHT);
		
		VXY_FORMULA(deadEnd, 1);
		deadEnd(vertex) <<= left(vertex, LEFT) && right(vertex, RIGHT) && up(vertex, UP) &&
			cellType(vertex).is(M_PASSABLE) && cellType(LEFT).is(M_IMPASSABLE) && cellType(RIGHT).is(M_IMPASSABLE) && cellType(UP).is(M_IMPASSABLE);
		deadEnd(vertex) <<= left(vertex, LEFT) && right(vertex, RIGHT) && down(vertex, DOWN) &&
			cellType(vertex).is(M_PASSABLE) && cellType(LEFT).is(M_IMPASSABLE) && cellType(RIGHT).is(M_IMPASSABLE) && cellType(DOWN).is(M_IMPASSABLE);
		deadEnd(vertex) <<= up(vertex, UP) && down(vertex, DOWN) && left(vertex, LEFT) &&
			cellType(vertex).is(M_PASSABLE) && cellType(UP).is(M_IMPASSABLE) && cellType(DOWN).is(M_IMPASSABLE) && cellType(LEFT).is(M_IMPASSABLE);
		deadEnd(vertex) <<= up(vertex, UP) && down(vertex, DOWN) && left(vertex, RIGHT) &&
			cellType(vertex).is(M_PASSABLE) && cellType(UP).is(M_IMPASSABLE) && cellType(DOWN).is(M_IMPASSABLE) && cellType(RIGHT).is(M_IMPASSABLE);
		
		// disallow a 2x2 block of walls
		Program::disallow(right(vertex, RIGHT) && down(vertex, DOWN) && right(DOWN, DOWN_RIGHT) &&
			cellType(vertex).is(M_IMPASSABLE) && cellType(RIGHT).is(M_IMPASSABLE) && cellType(DOWN).is(M_IMPASSABLE) && cellType(DOWN_RIGHT).is(M_IMPASSABLE)
		);
		// disallow a 2x2 block of empty
		Program::disallow(right(vertex, RIGHT) && down(vertex, DOWN) && right(DOWN, DOWN_RIGHT) &&
			cellType(vertex).is(M_PASSABLE) && cellType(RIGHT).is(M_PASSABLE) && cellType(DOWN).is(M_PASSABLE) && cellType(DOWN_RIGHT).is(M_PASSABLE)
		);
		
		// If two walls are on a diagonal of a 2 x 2 square, both common neighbors should not be empty.
		Program::disallow(right(vertex, RIGHT) && down(vertex, DOWN) && right(DOWN, DOWN_RIGHT) &&
			cellType(vertex).is(M_IMPASSABLE) && cellType(DOWN_RIGHT).is(M_IMPASSABLE) && cellType(RIGHT).is(M_PASSABLE) && cellType(DOWN).is(M_PASSABLE)
		);
		Program::disallow(right(vertex, RIGHT) && down(vertex, DOWN) && right(DOWN, DOWN_RIGHT) &&
			cellType(vertex).is(M_PASSABLE) && cellType(DOWN_RIGHT).is(M_PASSABLE) && cellType(RIGHT).is(M_IMPASSABLE) && cellType(DOWN).is(M_IMPASSABLE)
		);
		
		// hasAdjacentWall(x) is only true when there is an adjacent cell that is a wall.
		VXY_FORMULA(hasAdjacentWall, 1);
		hasAdjacentWall(vertex) <<= Program::graphEdge(vertex, X) && cellType(X).is(M_IMPASSABLE);
		
		// disallow walls that don't have any adjacent walls
		Program::disallow(cellType(vertex).is(M_IMPASSABLE) && ~border(vertex) && ~hasAdjacentWall(vertex));
		
		// Ensure keys are only in dead-ends
		Program::disallow(cellType(vertex).is(cellType.keys) && ~deadEnd(vertex));
	});

	VXY_DOMAIN_BEGIN(CellStepDomain)
		VXY_DOMAIN_VALUE(unreachable);
		VXY_DOMAIN_VALUE(reachable);
		VXY_DOMAIN_VALUE(origin);
	VXY_DOMAIN_END()

	struct StepResult
	{
		FormulaResult<2, CellStepDomain> stepCell;
		FormulaResult<3> edgeOpen;
	};
	
	// Defines each "step" of the maze: retrieving a key and unlocking a door. The final step is reaching the exit.
	auto stepProgram = Program::define([&](ProgramVertex vertex, int step)
	{
		VXY_VARIABLE(X); VXY_VARIABLE(Y);
				
		VXY_FORMULA(prevStep, 1);
		prevStep = Program::range(0, step-1);
		VXY_FORMULA(laterStep, 1);
		laterStep = Program::range(step+1, NUM_KEYS);

		// Define what is passable for this step: empty tiles and doors we have keys for 
		VXY_FORMULA(stepPassable, 2);
		stepPassable(vertex,step) <<= cellType(vertex).is(M_PASSABLE);
		stepPassable(vertex,step) <<= cellType(vertex).is(cellType.doors[X]) && prevStep(X);

		VXY_FORMULA(edgeOpen, 3);
		edgeOpen(X,Y,step) <<= Program::graphEdge(X, Y) && stepPassable(X,step) && stepPassable(Y,step);
		
		VXY_DOMAIN_FORMULA(stepCell, CellStepDomain, 2);
		auto M_STEP_REACHABLE = stepCell.reachable|stepCell.origin;
		
		stepCell(vertex,step).is(stepCell.origin) <<= cellType(vertex).is(cellType.entrance);
		stepCell(vertex,step).is(stepCell.reachable|stepCell.unreachable) <<= ~cellType(vertex).is(cellType.entrance);
		stepCell(vertex,step).is(stepCell.unreachable) <<= ~stepPassable(vertex,step);
		Program::disallow(stepCell(vertex,step).is(M_STEP_REACHABLE) && edgeOpen(vertex, X, step) && ~stepCell(X,step).is(M_STEP_REACHABLE));

		VXY_FORMULA(adjacentReachable, 2);
		adjacentReachable(vertex, step) <<= Program::graphEdge(X, vertex) && stepCell(X,step).is(M_STEP_REACHABLE);
		
		// Key for this step must be reachable
		Program::disallow(cellType(vertex).is(cellType.keys[step]) && ~stepCell(vertex,step).is(stepCell.reachable));
		// Door for this step must be reachable (not necessary because this is implicit from other constraints)
		// Program::disallow(cellType(vertex).is(cellType.doors[step]) && ~adjacentReachable(vertex));
		
		// Keys for later steps cannot be reachable 
		Program::disallow(cellType(vertex).is(cellType.keys[X]) && stepCell(vertex,step).is(stepCell.reachable) && laterStep(X));
		// Door for later steps cannot be reachable
		Program::disallow(cellType(vertex).is(cellType.doors[X]) && laterStep(X) && adjacentReachable(vertex,step));
		// If this is not the last step, the exit should not be reachable
		Program::disallow(step < NUM_KEYS && cellType(vertex).is(cellType.exit) && stepCell(vertex,step).is(stepCell.reachable));
		// If this is the last step, any empty tile (including exit) must be reachable.
		Program::disallow(step == NUM_KEYS && cellType(vertex).is(M_PASSABLE) && ~stepCell(vertex,step).is(M_STEP_REACHABLE));

		return StepResult{stepCell, edgeOpen};
	});
	
	ConstraintSolver solver(TEXT("mazeProgram"), seed);

	//
	// Allocate solver variables to link to cellType, and bind them.
	//

	auto cellDomain = CellDomain::get()->getSolverDomain();
	auto cells = solver.makeVariableGraph(TEXT("Grid"), ITopology::adapt(grid), cellDomain, TEXT("cell"));
	cellType.bind(solver, [&](const ProgramSymbol& vert)
	{
		return cells->get(vert.getInt());
	});
	
	//
	// Exactly one key/door per step
	//
	
	hash_map<int, tuple<int, int>> globalCardinalities;
	for (int step = 0; step < NUM_KEYS; ++step)
	{
		globalCardinalities[cellType.keys.getFirstValueIndex()+step] = make_tuple(1, 1);
		globalCardinalities[cellType.doors.getFirstValueIndex()+step] = make_tuple(1, 1);
	}
	solver.cardinality(cells->getData(), globalCardinalities);

	//
	// Add the programs to the solver.
	//
	
	auto entranceLocation = make_pair(1, 0);
	auto exitLocation = make_pair(numCols-2, numRows-1);
	auto baseProgramInst = baseProgram(ITopology::adapt(grid),
		grid->coordinateToIndex(entranceLocation.first, entranceLocation.second),
		grid->coordinateToIndex(exitLocation.first, exitLocation.second)
	);
	solver.addProgram(baseProgramInst);

	auto stepDomain = CellStepDomain::get()->getSolverDomain();
	auto edgeDomain = SolverVariableDomain(0, 1);
	for (int step = 0; step <= NUM_KEYS; ++step)
	{
		wstring stepName(wstring::CtorSprintf(), TEXT("Step-%d-TileVars"), step);
		auto stepData = solver.makeVariableGraph(stepName, ITopology::adapt(grid), stepDomain, {wstring::CtorSprintf(), TEXT("stepCell"), step});
		wstring stepEdgeName(wstring::CtorSprintf(), TEXT("Step-%d-EdgeVars"), step);
		auto stepEdgeData = solver.makeVariableGraph(stepEdgeName, ITopology::adapt(edges), edgeDomain, {wstring::CtorSprintf(), TEXT("stepEdge "), step});

		auto stepInst = stepProgram(ITopology::adapt(grid), step);
		auto& stepResult = stepInst->getResult();
		stepResult.stepCell.bind([&, stepData](const ProgramSymbol& vert, const ProgramSymbol& inStep)
		{
			return stepData->get(vert.getInt());
		});
		stepResult.edgeOpen.bind([&, stepEdgeData](const ProgramSymbol& startVert, const ProgramSymbol& endVert, const ProgramSymbol& inStep)
		{
			int edgeVert = edges->getVertexForSourceEdge(startVert.getInt(), endVert.getInt());
			return stepEdgeData->get(edgeVert);
		});
		
		solver.addProgram(stepInst);

		// Ensure reachability for this step
		auto reachabilityOrigin = vector{CellStepDomain::get()->origin.getValueIndex()};
		auto reachableMask = vector{CellStepDomain::get()->reachable.getValueIndex()};
		auto edgeBlockedMask = vector{0};
		solver.makeConstraint<ReachabilityConstraint>(stepData, reachabilityOrigin, reachableMask, stepEdgeData, edgeBlockedMask);
	}

	//
	// Create N solutions
	//
	
	for (int time = 0; time < times; ++time)
	{
		solver.solve();
		if (printVerbose)
		{
			print(cells, nullptr, solver);
		}
		solver.dumpStats(printVerbose);
		nErrorCount += check(cells, solver);
	}

	return nErrorCount;
}

// Less efficient version (for testing) that does not create graph constraints.
int MazeSolver::solveUsingProgram(int times, int numRows, int numCols, int seed, bool printVerbose)
{
	int nErrorCount = 0;

	auto grid = make_shared<PlanarGridTopology>(numCols, numRows);

	VXY_DOMAIN_BEGIN(CellDomain)
		VXY_DOMAIN_VALUE(blank);
		VXY_DOMAIN_VALUE(wall);
		VXY_DOMAIN_VALUE(entrance);
		VXY_DOMAIN_VALUE(exit);
		VXY_DOMAIN_VALUE_ARRAY(keys, NUM_KEYS);
		VXY_DOMAIN_VALUE_ARRAY(doors, NUM_KEYS);
	VXY_DOMAIN_END()

	VXY_DOMAIN_FORMULA(cellType, CellDomain, 2);

	const auto M_PASSABLE = cellType.blank|cellType.keys|cellType.entrance|cellType.exit;
	const auto M_IMPASSABLE = cellType.wall|cellType.doors;
	
	auto baseProgram = Program::define([&](int width, int height, int entranceX, int entranceY, int exitX, int exitY)
	{
		VXY_VARIABLE(X); VXY_VARIABLE(Y);
		
		VXY_FORMULA(col, 1);
		col = Program::range(0, width-1);
		VXY_FORMULA(row, 1);
		row = Program::range(0, height-1);

		VXY_FORMULA(gridTile, 2);
		gridTile(X,Y) <<= col(X) && row(Y);		
		
		cellType(X,Y).is(cellType.entrance) <<= X == entranceX && Y == entranceY;
		cellType(X,Y).is(cellType.exit) <<= X == exitX && Y == exitY;
		
		VXY_FORMULA(border, 2);
		border(0, Y) <<= row(Y);
		border(width-1, Y) <<= row(Y);
		border(X, 0) <<= col(X);
		border(X, height-1) <<= col(X);
		
		cellType(X,Y).is(cellType.wall|cellType.blank|cellType.keys|cellType.doors) <<= gridTile(X,Y) && ~border(X,Y);
		cellType(X,Y).is(cellType.wall) <<= border(X,Y) && ~cellType(X,Y).is(cellType.entrance|cellType.exit);

		VXY_FORMULA(deadEnd, 2);
		deadEnd(X,Y) <<= cellType(X,Y).is(M_PASSABLE) && cellType(X-1,Y).is(M_IMPASSABLE) && cellType(X+1,Y).is(M_IMPASSABLE) && cellType(X,Y-1).is(M_IMPASSABLE);
		deadEnd(X,Y) <<= cellType(X,Y).is(M_PASSABLE) && cellType(X-1,Y).is(M_IMPASSABLE) && cellType(X+1,Y).is(M_IMPASSABLE) && cellType(X,Y+1).is(M_IMPASSABLE);
		deadEnd(X,Y) <<= cellType(X,Y).is(M_PASSABLE) && cellType(X,Y-1).is(M_IMPASSABLE) && cellType(X,Y+1).is(M_IMPASSABLE) && cellType(X-1,Y).is(M_IMPASSABLE);
		deadEnd(X,Y) <<= cellType(X,Y).is(M_PASSABLE) && cellType(X,Y-1).is(M_IMPASSABLE) && cellType(X,Y+1).is(M_IMPASSABLE) && cellType(X+1,Y).is(M_IMPASSABLE);
		
		Program::disallow(cellType(X,Y).is(M_IMPASSABLE) && cellType(X+1,Y).is(M_IMPASSABLE) && cellType(X,Y+1).is(M_IMPASSABLE) && cellType(X+1,Y+1).is(M_IMPASSABLE));
		Program::disallow(cellType(X,Y).is(M_PASSABLE) && cellType(X+1,Y).is(M_PASSABLE) && cellType(X,Y+1).is(M_PASSABLE) && cellType(X+1,Y+1).is(M_PASSABLE));
		Program::disallow(cellType(X,Y).is(M_IMPASSABLE) && cellType(X+1,Y+1).is(M_IMPASSABLE) && cellType(X+1,Y).is(M_PASSABLE) && cellType(X,Y+1).is(M_PASSABLE));
		Program::disallow(cellType(X,Y).is(M_PASSABLE) && cellType(X+1,Y+1).is(M_PASSABLE) && cellType(X+1,Y).is(M_IMPASSABLE) && cellType(X,Y+1).is(M_IMPASSABLE));
		
		VXY_FORMULA(hasAdjacentWall, 2);
		hasAdjacentWall(X,Y) <<= cellType(X+1,Y).is(M_IMPASSABLE);
		hasAdjacentWall(X,Y) <<= cellType(X-1,Y).is(M_IMPASSABLE);
		hasAdjacentWall(X,Y) <<= cellType(X,Y+1).is(M_IMPASSABLE);
		hasAdjacentWall(X,Y) <<= cellType(X,Y-1).is(M_IMPASSABLE);
		
		Program::disallow(cellType(X,Y).is(M_IMPASSABLE) && ~border(X,Y) && ~hasAdjacentWall(X,Y));
		Program::disallow(cellType(X,Y).is(cellType.keys) && ~deadEnd(X,Y));
	});

	auto stepProgram = Program::define([&](int step)
	{
		VXY_VARIABLE(X); VXY_VARIABLE(Y); VXY_VARIABLE(Z);
				
		VXY_FORMULA(prevStep, 1);
		prevStep = Program::range(0, step-1);
		VXY_FORMULA(laterStep, 1);
		laterStep = Program::range(step+1, NUM_KEYS);
		
		VXY_FORMULA(stepPassable, 2);
		stepPassable(X,Y) <<= cellType(X,Y).is(M_PASSABLE);
		stepPassable(X,Y) <<= cellType(X,Y).is(cellType.doors[Z]) && prevStep(Z);
		
		VXY_FORMULA(stepReach, 2);
		stepReach(X,Y) <<= cellType(X,Y).is(cellType.entrance);
		stepReach(X,Y) <<= stepReach(X-1, Y) && stepPassable(X,Y);
		stepReach(X,Y) <<= stepReach(X+1, Y) && stepPassable(X,Y);
		stepReach(X,Y) <<= stepReach(X, Y-1) && stepPassable(X,Y);
		stepReach(X,Y) <<= stepReach(X, Y+1) && stepPassable(X,Y);
		
		VXY_FORMULA(adjacentReach, 2);
		adjacentReach(X-1,Y) <<= stepReach(X,Y);
		adjacentReach(X+1,Y) <<= stepReach(X,Y);
		adjacentReach(X,Y-1) <<= stepReach(X,Y);
		adjacentReach(X,Y+1) <<= stepReach(X,Y);
		
		Program::disallow(cellType(X,Y).is(cellType.keys[step]) && ~stepReach(X,Y));
		Program::disallow(cellType(X,Y).is(cellType.keys[Z]) && stepReach(X,Y) && laterStep(Z));
		Program::disallow(cellType(X,Y).is(cellType.doors[step]) && ~adjacentReach(X,Y));
		Program::disallow(cellType(X,Y).is(cellType.doors[Z]) && adjacentReach(X,Y) && laterStep(Z));
		Program::disallow(step < NUM_KEYS && cellType(X,Y).is(cellType.exit) && stepReach(X,Y));
		Program::disallow(step == NUM_KEYS && cellType(X,Y).is(M_PASSABLE) && ~stepReach(X,Y));
	});

	ConstraintSolver solver(TEXT("mazeProgram"), seed);
	
	SolverVariableDomain cellDomain = CellDomain::get()->getSolverDomain();
	auto cells = solver.makeVariableGraph(TEXT("Grid"), ITopology::adapt(grid), cellDomain, TEXT("cell"));
	cellType.bind(solver, [&](const ProgramSymbol& _x, const ProgramSymbol& _y)
	{
		int x = _x.getInt(), y = _y.getInt();
		return cells->get(grid->coordinateToIndex(x, y));
	});
	
	hash_map<int, tuple<int, int>> globalCardinalities;
	for (int step = 0; step < NUM_KEYS; ++step)
	{
		globalCardinalities[cellType.keys.getFirstValueIndex()+step] = make_tuple(1, 1);
		globalCardinalities[cellType.doors.getFirstValueIndex()+step] = make_tuple(1, 1);
	}
	solver.cardinality(cells->getData(), globalCardinalities);

	auto entranceLocation = make_pair(1, 0);
	auto exitLocation = make_pair(numCols-2, numRows-1);
	auto baseProgramInst = baseProgram(
		grid->getWidth(), grid->getHeight(),
		entranceLocation.first, entranceLocation.second,
		exitLocation.first, exitLocation.second
	);
	solver.addProgram(baseProgramInst);

	for (int step = 0; step <= NUM_KEYS; ++step)
	{
		auto stepInst = stepProgram(step);
		solver.addProgram(stepInst);
	}
	
	for (int time = 0; time < times; ++time)
	{
		solver.solve();
		if (printVerbose)
		{
			print(cells, nullptr, solver);
		}
		solver.dumpStats(printVerbose);
		nErrorCount += check(cells, solver);
	}

	return nErrorCount;
}

int MazeSolver::solveUsingRawConstraints(int times, int numRows, int numCols, int seed, bool printVerbose)
{
	int nErrorCount = 0;

	ConstraintSolver solver(TEXT("Maze"), seed);
	VERTEXY_LOG("TestMaze(%d)", solver.getSeed());

	//
	// Each tile of the map takes is one of these values:
	//
	constexpr int BLANK_IDX = 0;
	constexpr int WALL_IDX = 1;
	constexpr int ENTRANCE_IDX = 2;
	constexpr int EXIT_IDX = 3;
	// 4 ... NUM_KEYS are key tiles
	// 4+NUM_KEYS ... 4+NUM_KEYS*2 are door tiles

	// Predefined combinations of values for cells
	const vector<int> cell_Blank{BLANK_IDX};
	const vector<int> cell_Wall{WALL_IDX};
	const vector<int> cell_Entrance{ENTRANCE_IDX};
	const vector<int> cell_Exit{EXIT_IDX};

	vector<int> cell_Passable{BLANK_IDX, ENTRANCE_IDX, EXIT_IDX};
	vector<int> cell_Solid{WALL_IDX};

	vector<int> cell_Doors;
	vector<int> cell_Keys;
	for (int i = 0; i < NUM_KEYS; ++i)
	{
		cell_Keys.push_back(4 + i);
		cell_Doors.push_back(4 + i + NUM_KEYS);
	}
	cell_Passable.insert(cell_Passable.end(), cell_Keys.begin(), cell_Keys.end());
	cell_Solid.insert(cell_Solid.end(), cell_Doors.begin(), cell_Doors.end());

	// The domain determines the range of values that each tile takes on
	SolverVariableDomain tileDomain(0, 3 + NUM_KEYS + NUM_KEYS);

	// Create the topology for the maze.
	shared_ptr<PlanarGridTopology> grid = make_shared<PlanarGridTopology>(numCols, numRows);

	// Create a variable for each tile in the maze.
	auto tileData = solver.makeVariableGraph(TEXT("TileVars"), ITopology::adapt(grid), tileDomain, TEXT("Cell"));

	//
	// Set the initial potential values for each tile in the maze.
	//
	for (int y = 0; y < numRows; ++y)
	{
		for (int x = 0; x < numCols; ++x)
		{
			bool xBorder = x == 0 || x == numCols - 1;
			bool yBorder = y == 0 || y == numRows - 1;
			vector<int> allowedValues;
			if (xBorder && yBorder)
			{
				// Corner tile is always a wall
				allowedValues = {WALL_IDX};
			}
			else if (xBorder || yBorder)
			{
				// Border tile is either an entrance, exit, or wall
				allowedValues = vector{WALL_IDX, ENTRANCE_IDX, EXIT_IDX};
			}
			else
			{
				// Interior tile is either a wall, key, door, or blank
				allowedValues = vector{BLANK_IDX, WALL_IDX};
				allowedValues.insert(allowedValues.end(), cell_Keys.begin(), cell_Keys.end());
				allowedValues.insert(allowedValues.end(), cell_Doors.begin(), cell_Doors.end());
			}

			uint32_t node = grid->coordinateToIndex(x, y);
			solver.setInitialValues(tileData->get(node), allowedValues);
		}
	}

	//
	// Predefine entrance/exit
	// NOTE: This isn't necessary, but speeds up solving time. To have the solver choose
	// the entrance/exit itself, just comment out this block.
	//
	{
		int entrance1 = grid->coordinateToIndex(solver.randomRange(1, numCols - 2), 0);
		int entrance2 = grid->coordinateToIndex(solver.randomRange(1, numCols - 2), numRows - 1);
		solver.setInitialValues(tileData->get(entrance1), vector{2});
		solver.setInitialValues(tileData->get(entrance2), vector{3});
	}

	// Topology links allow you to specify relative coordinates in an arbitrary topology.
	// In this case, we want to get various neighbors of grid tiles.
	auto igrid = ITopology::adapt(grid);
	auto selfTile = make_shared<TVertexToDataGraphRelation<VarID>>(igrid, tileData);
	auto leftTile = make_shared<TTopologyLinkGraphRelation<VarID>>(igrid, tileData, PlanarGridTopology::moveLeft());
	auto rightTile = make_shared<TTopologyLinkGraphRelation<VarID>>(igrid, tileData, PlanarGridTopology::moveRight());
	auto upTile = make_shared<TTopologyLinkGraphRelation<VarID>>(igrid, tileData, PlanarGridTopology::moveUp());
	auto downTile = make_shared<TTopologyLinkGraphRelation<VarID>>(igrid, tileData, PlanarGridTopology::moveDown());
	auto downRightTile = make_shared<TTopologyLinkGraphRelation<VarID>>(igrid, tileData, PlanarGridTopology::moveDown().combine(PlanarGridTopology::moveRight()));

	//
	// DECLARE CONSTRAINTS
	//

	//
	// First up, define some rules about where wall and blank cells can be relative to each other.
	// We use graph constraints for this, which applies the constraint to every applicable tile. Applicable
	// tiles are those for which each relative coordinate is valid.
	//

	// CONSTRAINT: No 2x2 of solid tiles (solid = wall or door)
	solver.makeGraphConstraint<ClauseConstraint>(grid, ENoGood::NoGood,
		GraphRelationClause(selfTile, cell_Solid),
		GraphRelationClause(rightTile, cell_Solid),
		GraphRelationClause(downTile, cell_Solid),
		GraphRelationClause(downRightTile, cell_Solid)
	);

	// CONSTRAINT: No 2x2 of passable tiles (passable = blank, key, entrance, exit)
	solver.makeGraphConstraint<ClauseConstraint>(grid, ENoGood::NoGood,
		GraphRelationClause(selfTile, cell_Passable),
		GraphRelationClause(rightTile, cell_Passable),
		GraphRelationClause(downTile, cell_Passable),
		GraphRelationClause(downRightTile, cell_Passable)
	);

	// CONSTRAINT: No solid tiles with empty on either side (A)
	// [ ]
	//    [ ]
	solver.makeGraphConstraint<ClauseConstraint>(grid, ENoGood::NoGood,
		GraphRelationClause(selfTile, cell_Solid),
		GraphRelationClause(downRightTile, cell_Solid),
		GraphRelationClause(rightTile, cell_Passable),
		GraphRelationClause(downTile, cell_Passable)
	);
	// CONSTRAINT: No diagonal walls with empty on either side (B)
	//    [ ]
	// [ ]
	solver.makeGraphConstraint<ClauseConstraint>(grid, ENoGood::NoGood,
		GraphRelationClause(selfTile, cell_Passable),
		GraphRelationClause(rightTile, cell_Solid),
		GraphRelationClause(downTile, cell_Solid),
		GraphRelationClause(downRightTile, cell_Passable)
	);
	// CONSTRAINT: No solid tile entirely surrounded by empty on all sides
	solver.makeGraphConstraint<ClauseConstraint>(grid, ENoGood::NoGood,
		GraphRelationClause(selfTile, cell_Solid),
		GraphRelationClause(leftTile, cell_Passable),
		GraphRelationClause(rightTile, cell_Passable),
		GraphRelationClause(upTile, cell_Passable),
		GraphRelationClause(downTile, cell_Passable)
	);

	//
	//
	// CONSTRAINT: Exactly one entrance, one exit, one key/door per type
	//
	hash_map<int, tuple<int, int>> globalCardinalities;
	globalCardinalities[ENTRANCE_IDX] = make_tuple(1, 1); // Min = 1, Max = 1
	globalCardinalities[EXIT_IDX] = make_tuple(1, 1); // Min = 1, Max = 1
	for (int keyIdx : cell_Keys)
	{
		globalCardinalities[keyIdx] = make_tuple(1, 1);
	}
	for (int doorIdx : cell_Doors)
	{
		globalCardinalities[doorIdx] = make_tuple(1, 1);
	}
	solver.cardinality(tileData->getData(), globalCardinalities);

	//
	// The remaining constraints define how keys/doors work, and ensure that the maze is solveable.
	//

	// CONSTRAINT: Doors must be adjacent to exactly two walls, not on a corner
	// (Technically these constraints aren't required, but speed up solution time)
	for (auto& dir : {upTile, downTile})
	{
		solver.makeGraphConstraint<ClauseConstraint>(grid, ENoGood::NoGood,
			GraphRelationClause(rightTile, cell_Solid),
			GraphRelationClause(dir, cell_Solid),
			GraphRelationClause(selfTile, cell_Doors)
		);
		solver.makeGraphConstraint<ClauseConstraint>(grid, ENoGood::NoGood,
			GraphRelationClause(leftTile, cell_Passable),
			GraphRelationClause(dir, cell_Passable),
			GraphRelationClause(selfTile, cell_Doors)
		);
	}

	//
	//  CONSTRAINT: Keys can only be placed in dead-ends
	//

	for (auto& dir1 : {leftTile, rightTile})
	{
		for (auto& dir2 : {upTile, downTile})
		{
			solver.makeGraphConstraint<ClauseConstraint>(grid, ENoGood::NoGood,
				GraphRelationClause(selfTile, cell_Keys),
				GraphRelationClause(dir1, EClauseSign::Outside, cell_Wall),
				GraphRelationClause(dir2, EClauseSign::Outside, cell_Wall)
			);
		}
	}
	solver.makeGraphConstraint<ClauseConstraint>(grid, ENoGood::NoGood,
		GraphRelationClause(selfTile, cell_Keys),
		GraphRelationClause(leftTile, EClauseSign::Outside, cell_Wall),
		GraphRelationClause(rightTile, EClauseSign::Outside, cell_Wall)
	);
	solver.makeGraphConstraint<ClauseConstraint>(grid, ENoGood::NoGood,
		GraphRelationClause(selfTile, cell_Keys),
		GraphRelationClause(upTile, EClauseSign::Outside, cell_Wall),
		GraphRelationClause(downTile, EClauseSign::Outside, cell_Wall)
	);

	//
	// Define a domain for edges between tiles. Each edge is either solid or empty.
	//

	// Edge graphs per step: 0 = passable, 1 = impassable
	SolverVariableDomain edgeDomain(0, 1);
	const vector<int> edge_Empty{0};
	const vector<int> edge_Solid{1};

	// Create the edge topology for the maze. This creates a parallel graph where each node in Edges corresponds
	// to an edge in Grid.
	auto edges = make_shared<EdgeTopology>(ITopology::adapt(grid), true, false);

	//
	// Constrain how the maze must be solved. We want to require that:
	// 1. The player must acquire all keys and unlock all doors to reach the exit.
	// 2. The player can only reach the keys in an exact order.
	// 3. Once all keys have been reached, the player can visit any empty tile in the maze.
	// 4. The player can reach the exit.
	//
	// To do this, we define a series of "steps". At step 0, the player has no keys. At step 1, the player has
	// the first key; at step 2 the player has the second key, and so on. For the final step, the player
	// should be able to reach the door.
	//


	// For each step, for each tile, we're going to create a variable with one of these values:
	SolverVariableDomain stepDomain(0, 2);
	const vector<int> step_Reachable{0}; // The tile is/must be reachable from this step.
	const vector<int> step_Unreachable{1}; // The tile is/must be unreachable from this step.
	const vector<int> step_Origin{2}; // This is the entrance to the maze.
	const vector<int> step_ReachableOrOrigin{0, 2}; // This tile is passable at this step (either entrance or reachable)

	//
	// Create constraints for each step. (Number of keys + final step to reach exit)
	//
	vector<shared_ptr<TTopologyVertexData<VarID>>> stepDatas;
	vector<shared_ptr<TTopologyVertexData<VarID>>> stepEdgeDatas;
	for (int step = 0; step < NUM_KEYS + 1; ++step)
	{
		//
		// Make the grid of variables for this step
		//
		wstring stepName(wstring::CtorSprintf(), TEXT("Step-%d-TileVars"), step);
		auto stepData = solver.makeVariableGraph(stepName, ITopology::adapt(grid), stepDomain, {wstring::CtorSprintf(), TEXT("Step%d-"), step});
		stepDatas.push_back(stepData);

		auto selfStepTile = make_shared<TVertexToDataGraphRelation<VarID>>(igrid, stepData);

		// If this tile is the entrance in the maze, constrain it to be the entrance in this step.
		solver.makeGraphConstraint<IffConstraint>(grid,
			GraphRelationClause(selfStepTile, step_Origin),
			vector{GraphRelationClause(selfTile, cell_Entrance)}
		);

		// A tile can never be passable in any step if it is a wall
		solver.makeGraphConstraint<ClauseConstraint>(grid, ENoGood::NoGood,
			GraphRelationClause(selfStepTile, step_ReachableOrOrigin),
			GraphRelationClause(selfTile, cell_Wall)
		);

		// If we don't have all the keys at this step...
		if (step < NUM_KEYS)
		{
			// Prohibit the key for this step being unreachable
			solver.makeGraphConstraint<ClauseConstraint>(grid, ENoGood::NoGood,
				GraphRelationClause(selfStepTile, EClauseSign::Outside, step_Reachable),
				GraphRelationClause(selfTile, {cell_Keys[step]})
			);
		}
		else
		{
			// On last step, all blank cells should be reachable
			solver.makeGraphConstraint<ClauseConstraint>(grid, ENoGood::NoGood,
				GraphRelationClause(selfStepTile, EClauseSign::Outside, step_Reachable),
				GraphRelationClause(selfTile, cell_Blank)
			);
		}

		// Don't allow keys in later steps to be reachable.
		for (int j = step + 1; j < NUM_KEYS; ++j)
		{
			solver.makeGraphConstraint<ClauseConstraint>(grid, ENoGood::NoGood,
				GraphRelationClause(selfStepTile, step_Reachable),
				GraphRelationClause(selfTile, {cell_Keys[j]})
			);
		}

		if (step > 0)
		{
			auto PrevStepTile = make_shared<TTopologyLinkGraphRelation<VarID>>(igrid, stepDatas[step - 1], TopologyLink::SELF);
			// Optimization: Later step's tile is always reachable if earlier step's tile is reachable.
			solver.makeGraphConstraint<ClauseConstraint>(grid, ENoGood::NoGood,
				GraphRelationClause(selfStepTile, EClauseSign::Outside, step_Reachable),
				GraphRelationClause(PrevStepTile, step_Reachable)
			);
		}

		// Only allow exit to be reachable on last step
		solver.makeGraphConstraint<ClauseConstraint>(grid, ENoGood::NoGood,
			GraphRelationClause(selfStepTile, EClauseSign::Outside, step < NUM_KEYS ? step_Unreachable : step_Reachable),
			GraphRelationClause(selfTile, cell_Exit)
		);

		//
		// Define navigability for this step. Each step has its own set of boolean variables for each edge, representing
		// whether that edge is open (traversable) or not.
		//
		wstring stepEdgesName(wstring::CtorSprintf(), TEXT("Step-%d-EdgeVars"), step);
		auto stepEdgeData = solver.makeVariableGraph(stepEdgesName, ITopology::adapt(edges), edgeDomain, {wstring::CtorSprintf(), TEXT("Step%d-Edge "), step});
		stepEdgeDatas.push_back(stepEdgeData);

		auto edgeNodeToEdgeVarRel = make_shared<TVertexToDataGraphRelation<VarID>>(igrid, stepEdgeData);
		for (int direction : {PlanarGridTopology::Left, PlanarGridTopology::Right, PlanarGridTopology::Up, PlanarGridTopology::Down})
		{
			// Relations: Maps a node index in Grid to a node index in Edges
			auto tileToOutgoingEdgeNodeRel = make_shared<TVertexEdgeToEdgeGraphVertexGraphRelation<PlanarGridTopology, false>>(grid, edges, direction);
			auto tileToIncomingEdgeNodeRel = make_shared<TVertexEdgeToEdgeGraphVertexGraphRelation<PlanarGridTopology, true>>(grid, edges, direction);
			// Map node index in grid to an Edge variable
			auto outgoingEdgeVarRel = tileToOutgoingEdgeNodeRel->map(edgeNodeToEdgeVarRel);
			auto incomingEdgeVarRel = tileToIncomingEdgeNodeRel->map(edgeNodeToEdgeVarRel);

			// Map from an edge node to the Tile variable on other side of edge
			auto destTile = make_shared<TTopologyLinkGraphRelation<VarID>>(igrid, tileData, TopologyLink::create(make_tuple(direction, 1)));
			auto destStepTile = make_shared<TTopologyLinkGraphRelation<VarID>>(igrid, stepData, TopologyLink::create(make_tuple(direction, 1)));

			// Edges toward walls are always solid
			solver.makeGraphConstraint<ClauseConstraint>(grid, ENoGood::NoGood,
				GraphRelationClause(selfTile, cell_Wall),
				GraphRelationClause(incomingEdgeVarRel, EClauseSign::Outside, edge_Solid)
			);

			// Edges between passable cells are always empty
			solver.makeGraphConstraint<ClauseConstraint>(grid, ENoGood::NoGood,
				GraphRelationClause(selfTile, cell_Passable),
				GraphRelationClause(destTile, cell_Passable),
				GraphRelationClause(outgoingEdgeVarRel, EClauseSign::Outside, edge_Empty)
			);

			// If a tile is reachable at this step, and there is an open edge to a neighboring tile, that tile is
			// also be reachable this step.
			solver.makeGraphConstraint<ClauseConstraint>(grid, ENoGood::NoGood,
				GraphRelationClause(selfStepTile, step_ReachableOrOrigin),
				GraphRelationClause(outgoingEdgeVarRel, edge_Empty),
				GraphRelationClause(destStepTile, EClauseSign::Outside, step_ReachableOrOrigin)
			);

			// Ensure any edges that lead to locked doors (for keys we don't have) are marked solid.
			for (int j = step; j < NUM_KEYS; ++j)
			{
				solver.makeGraphConstraint<ClauseConstraint>(grid, ENoGood::NoGood,
					GraphRelationClause(incomingEdgeVarRel, EClauseSign::Outside, edge_Solid),
					GraphRelationClause(selfTile, {cell_Doors[j]})
				);
			}
		}

		// Ensure reachability for this step: all Step_Reachable cells must be reachable from Step_Origin cells.
		solver.makeConstraint<ReachabilityConstraint>(stepData, step_Origin, step_Reachable, stepEdgeData, edge_Solid);
	}

	// Uncomment to print out the maze every time the solver backtracks (for debugging)
	// {
	// 	auto debugStrat = make_shared<DebugMazeStrategy>(solver, tileData, stepEdgeDatas.back());
	// 	solver.addDecisionHeuristic(debugStrat);
	// }

	shared_ptr<SolverDecisionLog> outputLog;
	if constexpr (WRITE_BREADCRUMB_LOG)
	{
		outputLog = make_shared<SolverDecisionLog>();
	}
	// {
	// 	auto inputLog = make_shared<SolverDecisionLog>();
	// 	cs_verify(inputLog->read(TEXT("DecisionLog.txt")));
	// 	auto LogHeuristic = make_shared<LogOrderHeuristic>(solver, inputLog);
	// 	solver.addDecisionHeuristic(LogHeuristic);
	// }

	if (outputLog != nullptr)
	{
		solver.setOutputLog(outputLog);
	}

	//
	// Solve!
	//
	for (int i = 0; i < times; ++i)
	{
		EConstraintSolverResult result = solver.startSolving();
		while (result == EConstraintSolverResult::Unsolved)
		{
			if (ATTEMPT_SOLUTION_AT >= 0 && solver.getStats().stepCount >= ATTEMPT_SOLUTION_AT)
			{
				solver.debugAttemptSolution(TEXT("MazeSolution.txt"));
			}

			result = solver.step();
			// print out the maze every MAZE_REFRESH_RATE steps
			if (printVerbose && MAZE_REFRESH_RATE > 0 && (solver.getStats().stepCount % MAZE_REFRESH_RATE) == 0)
			{
				print(stepDatas[0], stepEdgeDatas[0], solver);
			}
		}

		// Print out the final maze!
		if (printVerbose)
		{
			print(tileData, stepEdgeDatas.back(), solver);
		}
		solver.dumpStats(printVerbose);
		EATEST_VERIFY(result == EConstraintSolverResult::Solved);

		// Ensure the maze is actually valid!
		// (For now only checking if we can reach the exit from the entrance when all doors are unlocked)
		nErrorCount += check(tileData, solver);

		if (WRITE_SOLUTION_FILE && result == EConstraintSolverResult::Solved)
		{
			solver.debugSaveSolution(TEXT("MazeSolution.txt"));
		}
	}

	if (outputLog != nullptr)
	{
		outputLog->writeBreadcrumbs(solver, TEXT("DecisionLog.txt"));
	}

	return nErrorCount;
}

int MazeSolver::check(const shared_ptr<TTopologyVertexData<VarID>>& tileData, const ConstraintSolver& solver)
{
	int nErrorCount = 0;

	auto grid = tileData->getSource()->getImplementation<PlanarGridTopology>();

	vector<bool> reachable;
	reachable.resize(grid->getNumVertices(), false);

	int entrance = -1;
	for (int i = 0; i < grid->getNumVertices(); ++i)
	{
		if (solver.getSolvedValue(tileData->get(i)) == 2)
		{
			entrance = i;
			break;
		}
	}
	EATEST_VERIFY_F(entrance >= 0, "No Entrance! Seed %d", solver.getSeed());

	// flood fill to find all reachable from entrance
	BreadthFirstSearchAlgorithm breadthFirstSearchAlgorithm;
	breadthFirstSearchAlgorithm.search(*grid.get(), entrance, [&](int, int node, int)
	{
		if (solver.getSolvedValue(tileData->get(node)) == 1)
		{
			return ETopologySearchResponse::Skip;
		}
		reachable[node] = true;
		return ETopologySearchResponse::Continue;
	});

	// Ensure all non-solid cells are reachable
	vector<int> solidTypes = {1};
	for (int i = 0; i < NUM_KEYS; ++i)
	{
		solidTypes.push_back(3 + NUM_KEYS + i);
	}

	for (int i = 0; i < grid->getNumVertices(); ++i)
	{
		if (!contains(solidTypes.begin(), solidTypes.end(), solver.getSolvedValue(tileData->get(i))))
		{
			int x, y;
			grid->indexToCoordinate(i, x, y);
			EATEST_VERIFY_F(reachable[i], "Cell %dx%d not reachable! Seed %d", x, y, solver.getSeed());
		}
	}

	return nErrorCount;
}

void MazeSolver::print(const shared_ptr<TTopologyVertexData<VarID>>& cells, const shared_ptr<TTopologyVertexData<VarID>>& edges, const ConstraintSolver& solver)
{
	auto& grid = cells->getSource()->getImplementation<PlanarGridTopology>();
	int numCols = grid->getWidth();
	int numRows = grid->getHeight();

	auto& edgeTopology = edges != nullptr
		? edges->getSource()->getImplementation<EdgeTopology>()
		: nullptr;

	wstring out = TEXT("       ");
	for (int x = 0; x < numCols; ++x)
	{
		out.append_sprintf(TEXT("%d"), x);
		if (x < 10)
		{
			out += TEXT(" ");
		}
		if (x < 100)
		{
			out += TEXT(" ");
		}
		if (PRINT_EDGES && edgeTopology != nullptr && x < 1000)
		{
			out += TEXT(" ");
		}
	}
	VERTEXY_LOG("%s", out.c_str());

	for (int y = 0; y < numRows; ++y)
	{
		if (PRINT_EDGES && edgeTopology != nullptr && y != 0)
		{
			out.sprintf(TEXT("%d  "), y - 1);
			if (y < 10)
			{
				out += TEXT(" ");
			}
			if (y < 100)
			{
				out += TEXT(" ");
			}
			if (y < 1000)
			{
				out += TEXT(" ");
			}
			for (int x = 0; x < numCols; ++x)
			{
				int node = grid->coordinateToIndex(x, y);
				int upNode = grid->coordinateToIndex(x, y - 1);
				int edgeNode = edgeTopology->getVertexForSourceEdge(node, upNode);
				vector<int> edgeVals = solver.getPotentialValues(edges->get(edgeNode));
				if (edgeVals.empty())
				{
					out += TEXT("!!! ");
				}
				else if (edgeVals.size() == 1 && edgeVals[0] != 0)
				{
					out += TEXT("--- ");
				}
				else if (edgeVals.size() == 1 && edgeVals[0] == 0)
				{
					out += TEXT("ooo ");
				}
				else
				{
					out += TEXT("    ");
				}
			}
			VERTEXY_LOG("%s", out.c_str());
		}

		out.sprintf(TEXT("%d  "), y);
		if (y < 10)
		{
			out += TEXT(" ");
		}
		if (y < 100)
		{
			out += TEXT(" ");
		}
		if (PRINT_EDGES && edgeTopology != nullptr && y < 1000)
		{
			out += TEXT(" ");
		}

		for (int x = 0; x < numCols; ++x)
		{
			int node = grid->coordinateToIndex(x, y);
			if (PRINT_EDGES && edgeTopology != nullptr && x != 0)
			{
				int leftNode = grid->coordinateToIndex(x - 1, y);
				int edgeNode = edgeTopology->getVertexForSourceEdge(node, leftNode);
				vector<int> edgeVals = solver.getPotentialValues(edges->get(edgeNode));
				if (edgeVals.empty())
				{
					out += TEXT("!");
				}
				else if (edgeVals.size() == 1 && edgeVals[0] != 0)
				{
					out += TEXT("|");
				}
				else if (edgeVals.size() == 1 && edgeVals[0] == 0)
				{
					out += TEXT("o");
				}
				else
				{
					out += TEXT(" ");
				}
			}

			vector<int> cellVals = solver.getPotentialValues(cells->get(node));
			if (cellVals.size() == 1)
			{
				int cell = cellVals[0];
				if (cell == 0)
				{
					out += TEXT("   ");
				}
				else if (cell == 1)
				{
					out += TEXT("[ ]");
				}
				else if (cell == 2)
				{
					out += TEXT(" e ");
				}
				else if (cell == 3)
				{
					out += TEXT(" E ");
				}
				else if (cell <= 3 + NUM_KEYS)
				{
					out.append_sprintf(TEXT(" %d "), cell - 3);
				}
				else
				{
					out.append_sprintf(TEXT("[%d]"), cell - NUM_KEYS - 3);
				}
			}
			else if (cellVals.size() == 0)
			{
				out += TEXT("!!!");
			}
			else
			{
				out += TEXT(" . ");
			}
		}

		VERTEXY_LOG("%s", out.c_str());
	}
}
