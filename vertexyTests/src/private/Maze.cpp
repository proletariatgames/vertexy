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
static constexpr int NUM_KEYS = 3;
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


int MazeSolver::solveProgram(int times, int numRows, int numCols, int seed, bool printVerbose)
{
	int nErrorCount = 0;

	struct MazeResult
	{
		FormulaResult<1> wall;
		FormulaResult<1> blank;
		FormulaResult<1> exit;
		FormulaResult<1> entrance;
	};

	auto grid = make_shared<PlanarGridTopology>(numCols, numRows);

	// Rule formulas can only be defined within a Program::define() block
	auto simpleMaze = Program::define([](ProgramVertex vertex, ProgramSymbol entranceVertex, ProgramSymbol exitVertex)
	{
		// Floating variables. These don't mean anything outside the context of a rule statement.
		// Within a rule statement, they encode equality. E.g. if "X" shows up in two places in a rule,
		// it means that those Xs are the same. See below.
		VXY_VARIABLE(X);
		VXY_VARIABLE(Y);

		// define the entrance/exit positions, based on the program inputs.
		VXY_FORMULA(entrance, 1);
		VXY_FORMULA(exit, 1);
		entrance(entranceVertex);
		exit(exitVertex);

		// Define a rule formula border(x,y), which is only true at the edges of the map.
		VXY_FORMULA(border, 1);
		border(vertex) <<= ~Program::hasGraphLink(vertex, PlanarGridTopology::moveUp());
		border(vertex) <<= ~Program::hasGraphLink(vertex, PlanarGridTopology::moveDown());
		border(vertex) <<= ~Program::hasGraphLink(vertex, PlanarGridTopology::moveLeft());
		border(vertex) <<= ~Program::hasGraphLink(vertex, PlanarGridTopology::moveRight());
		
		VXY_FORMULA(wall, 1);
		VXY_FORMULA(blank, 1);
		// wall OR blank may be true if this is a grid tile that is not on the border, and not the entrance or exit.
		(wall(vertex) | blank(vertex)) <<= ~border(vertex) && ~entrance(vertex) && ~exit(vertex);
		// border is a wall as long as it's not the entrance or exit.
		wall(vertex) <<= border(vertex) && ~entrance(vertex) && ~exit(vertex);

		// declare the type of tiles that are considered empty
		VXY_FORMULA(emptyType, 1);
		emptyType(vertex) <<= blank(vertex);
		emptyType(vertex) <<= entrance(vertex);
		emptyType(vertex) <<= exit(vertex);

		// // disallow a 2x2 block of walls
		// Program::disallow(wall(X,Y) && wall(X1, Y) && wall(X, Y1) && wall(X1, Y1) && X1 == X+1 && Y1 == Y+1);
		// // disallow a 2x2 block of empty
		// Program::disallow(emptyType(X,Y) && emptyType(X1, Y) && emptyType(X, Y1) && emptyType(X1, Y1) && X1 == X+1 && Y1 == Y+1);
		//
		// // If two walls are on a diagonal of a 2 x 2 square, both common neighbors should not be empty.
		// Program::disallow(wall(X,Y) && wall(X+1,Y+1) && emptyType(X+1,Y) && emptyType(X, Y+1));
		// Program::disallow(emptyType(X, Y) && emptyType(X+1, Y+1) && wall(X+1,Y) && wall(X,Y+1));
		//
		// // wallWithAdjacentWall(x,y) is only true when there is an adjacent cell that is a wall.
		// VXY_FORMULA(wallWithAdjacentWall, 2);
		// wallWithAdjacentWall(X,Y) <<= wall(X,Y) && adjacent(X, Y, X1, Y1) && wall(X1, Y1);
		//
		// // disallow walls that don't have any adjacent walls
		// Program::disallow(wall(X,Y) && ~border(X,Y) && ~wallWithAdjacentWall(X,Y));

		// encode reachability (faster to do this with a reachability constraint)
		VXY_FORMULA(reach, 1);
		reach(vertex) <<= entrance(vertex);
		reach(vertex) <<= reach(X) && emptyType(vertex) && Program::graphEdge(X, vertex);
		Program::disallow(emptyType(vertex) && ~reach(vertex));

		return MazeResult {wall, blank, entrance, exit};
	});

	ConstraintSolver solver(TEXT("mazeProgram"), seed);

	// Instantiate the program by supplying its input arguments.
	// A program can be instantiated multiple times.
	auto entranceLocation = make_pair(1, 0);
	auto exitLocation = make_pair(numCols-2, numRows-1);
	auto inst = simpleMaze(ITopology::adapt(grid),
		grid->coordinateToIndex(entranceLocation.first, entranceLocation.second),
		grid->coordinateToIndex(exitLocation.first, exitLocation.second)
	);

	// Grab the results from the instantiation.
	MazeResult& result = get<MazeResult>(inst);

	// Allocate solver variables to link to the MazeResult.
	// This is not strictly necessary - the solver will determine a solution for all rules regardless.
	// However this has two advantages:
	// 1) We can easily access the variables we care about by binding them to the program's output.
	// 2) We can combine multiple (boolean, mutually-exclusive) variables into a single solver non-boolean variable.

	constexpr int BLANK_IDX = 0;
	constexpr int WALL_IDX = 1;
	constexpr int ENTRANCE_IDX = 2;
	constexpr int EXIT_IDX = 3;

	// Create the grid representation and solver variables for each cell in the grid.
	SolverVariableDomain cellDomain(0, 3);
	auto cells = solver.makeVariableGraph(TEXT("Grid"), ITopology::adapt(grid), cellDomain, TEXT("cell"));

	//
	// Bind the formulas to variables
	//

	result.wall.bind([&](const ProgramSymbol& vert)
	{
		return SignedClause(cells->get(vert.getInt()), vector{WALL_IDX});
	});

	result.blank.bind([&](const ProgramSymbol& vert)
	{
		return SignedClause(cells->get(vert.getInt()), vector{BLANK_IDX});
	});

	result.entrance.bind([&](const ProgramSymbol& vert)
	{
		return SignedClause(cells->get(vert.getInt()), vector{ENTRANCE_IDX});
	});

	result.exit.bind([&](const ProgramSymbol& vert)
	{
		return SignedClause(cells->get(vert.getInt()), vector{EXIT_IDX});
	});

	// Add the program to the solver. Multiple programs can be added, as well
	// as any other constraints.
	solver.addProgram(move(inst));

	// Create N solutions
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

int MazeSolver::solveKeyDoor(int times, int numRows, int numCols, int seed, bool printVerbose)
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
	int numCols = grid->GetWidth();
	int numRows = grid->GetHeight();

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
