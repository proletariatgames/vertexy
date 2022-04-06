// Copyright Proletariat, Inc. All Rights Reserved.
#include "NQueens.h"

#include "ConstraintSolver.h"
#include "../../ThirdParty/eastl/test/packages/EATest/include/EATest/EATest.h"
#include "constraints/TableConstraint.h"
#include "constraints/IffConstraint.h"
#include "topology/GridTopology.h"
#include "topology/IPlanarTopology.h"
#include "topology/GraphRelations.h"
#include "variable/SolverVariableDomain.h"

using namespace csolverTests;

int NQueensSolvers::solveUsingAllDifferent(int times, int n, int seed, bool printVerbose)
{
	int nErrorCount = 0;
	for (int time = 0; time < times; ++time)
	{
		ConstraintSolver solver(TEXT("Queens-AllDifferent"), seed);

		int maxTile = n - 1;
		SolverVariableDomain domainX(0, maxTile);
		SolverVariableDomain domainY(-maxTile, maxTile);
		SolverVariableDomain domainZ(0, maxTile * 2);

		vector<VarID> xs;
		vector<VarID> ys;
		vector<VarID> zs;

		for (int i = 0; i < n; ++i)
		{
			xs.push_back(solver.makeVariable({wstring::CtorSprintf(), TEXT("X%d"), i}, domainX));
			ys.push_back(solver.makeVariable({wstring::CtorSprintf(), TEXT("Y%d"), i}, domainY));
			zs.push_back(solver.makeVariable({wstring::CtorSprintf(), TEXT("Z%d"), i}, domainZ));
		}

		for (int i = 0; i < n; ++i)
		{
			solver.offset(ys[i], xs[i], -1 - i);
			solver.offset(zs[i], xs[i], i + 1);
		}

		solver.allDifferent(xs);
		solver.allDifferent(ys);
		solver.allDifferent(zs);

		solver.solve();
		solver.dumpStats(printVerbose);

		EATEST_VERIFY(solver.getCurrentStatus() == EConstraintSolverResult::Solved);
		if (printVerbose)
		{
			print(n, &solver, xs);
		}
		nErrorCount += check(n, &solver, xs);
	}
	return nErrorCount;
}

int NQueensSolvers::solveUsingTable(int times, int n, int seed, bool printVerbose)
{
	int nErrorCount = 0;
	for (int time = 0; time < times; ++time)
	{
		ConstraintSolver solver(TEXT("NQueens-Table"), seed);

		SolverVariableDomain domain(0, n - 1);
		vector<VarID> variables;
		for (int i = 0; i < n; ++i)
		{
			variables.push_back(solver.makeVariable({wstring::CtorSprintf(), TEXT("Queen%d"), i}, domain));
		}

		// Create tuples to represent column exclusivity (i.e. alldifferent constraint)
		TableConstraintDataPtr verticalTuples = make_shared<TableConstraintData>();
		for (int col = 0; col < n; ++col)
		{
			for (int col2 = 0; col2 < n; ++col2)
			{
				if (col2 != col)
				{
					verticalTuples->tupleRows.push_back({col, col2});
				}
			}
		}

		for (int i = 0; i < n; ++i)
		{
			for (int j = i + 1; j < n; ++j)
			{
				solver.table(verticalTuples, {variables[i], variables[j]});
			}
		}

		// For each row (queen), prohibit queens on other rows from occupying columns on our diagonals
		for (int row = 0; row < n; ++row)
		{
			for (int otherRow = row + 1; otherRow < n; ++otherRow)
			{
				int diagOffs = abs(otherRow - row);

				auto diagTuples = make_shared<TableConstraintData>();
				for (int col = 0; col < n; ++col)
				{
					for (int otherCol = 0; otherCol < n; ++otherCol)
					{
						if (otherCol != col && otherCol != col + diagOffs && otherCol != col - diagOffs)
						{
							diagTuples->tupleRows.push_back({col, otherCol});
						}
					}
				}

				solver.table(diagTuples, {variables[row], variables[otherRow]});
			}
		}

		solver.solve();
		solver.dumpStats(printVerbose);

		EATEST_VERIFY(solver.getCurrentStatus() == EConstraintSolverResult::Solved);
		if (printVerbose)
		{
			print(n, &solver, variables);
		}
		nErrorCount += check(n, &solver, variables);
	}
	return nErrorCount;
}

int NQueensSolvers::solveUsingGraph(int times, int n, int seed, bool printVerbose)
{
	int nErrorCount = 0;
	for (int time = 0; time < times; ++time)
	{
		ConstraintSolver solver(TEXT("NQueens-Graph"), seed);

		SolverVariableDomain domain(0, n - 1);
		auto queenGraph = make_shared<PlanarGridTopology>(1, n);
		auto queenGraphData = solver.makeVariableGraph(TEXT("Queens"), IPlanarTopology::adapt(queenGraph), domain, TEXT("QueenRow"));

		SolverVariableDomain tileDomain(0, 1);
		auto tileGrid = make_shared<PlanarGridTopology>(n, n);

		auto tileGridData = solver.makeVariableGraph(TEXT("Tiles"), IPlanarTopology::adapt(tileGrid), tileDomain, TEXT("Tile"));
		auto selfRelation = make_shared<TTopologyLinkGraphRelation<VarID>>(tileGridData, TopologyLink::SELF);

		auto tile_On = vector{1};

		//
		// Link Tiles to be "on" iff a queen is on them
		//

		// Encodes relation of a given Tile -> that row's Queen on the tile.
		class TileQueenRelation : public IGraphRelation<SignedClause>
		{
		public:
			TileQueenRelation(const ConstraintSolver& solver, const shared_ptr<PlanarGridTopology>& topo, const shared_ptr<TTopologyVertexData<VarID>>& queens)
				: m_solver(solver)
				, m_topology(topo)
				, m_queens(queens)
			{
			}

			virtual wstring toString() const override { return TEXT("RowToQueen"); }

			virtual bool getRelation(int sourceNode, SignedClause& out) const override
			{
				int col, row;
				m_topology->indexToCoordinate(sourceNode, col, row);

				out.variable = m_queens->get(row);
				out.values = {col};
				return true;
			}

		protected:
			const ConstraintSolver& m_solver;
			shared_ptr<PlanarGridTopology> m_topology;
			shared_ptr<TTopologyVertexData<VarID>> m_queens;
		};

		solver.makeGraphConstraint<IffConstraint>(tileGrid,
			GraphRelationClause(selfRelation, tile_On),
			vector<GraphClauseRelationPtr>{make_shared<TileQueenRelation>(solver, tileGrid, queenGraphData)}
		);

		// board constraints
		GraphRelationClause self_Off(selfRelation, EClauseSign::Outside, tile_On);
		for (int i = 1; i < n; ++i)
		{
			auto upRelation = make_shared<TTopologyLinkGraphRelation<VarID>>(tileGridData, PlanarGridTopology::moveUp(i));
			auto downRelation = make_shared<TTopologyLinkGraphRelation<VarID>>(tileGridData, PlanarGridTopology::moveDown(i));
			auto downRightRelation = make_shared<TTopologyLinkGraphRelation<VarID>>(tileGridData, PlanarGridTopology::moveDown(i).combine(PlanarGridTopology::moveRight(i)));
			auto downLeftRelation = make_shared<TTopologyLinkGraphRelation<VarID>>(tileGridData, PlanarGridTopology::moveDown(i).combine(PlanarGridTopology::moveLeft(i)));

			solver.makeGraphConstraint<ClauseConstraint>(tileGrid, vector{
				self_Off, GraphRelationClause(downRelation, EClauseSign::Outside, tile_On)
			});

			solver.makeGraphConstraint<ClauseConstraint>(tileGrid, vector{
				self_Off, GraphRelationClause(downRightRelation, EClauseSign::Outside, tile_On)
			});

			solver.makeGraphConstraint<ClauseConstraint>(tileGrid, vector{
				self_Off, GraphRelationClause(downLeftRelation, EClauseSign::Outside, tile_On)
			});
		}

		solver.solve();
		solver.dumpStats(printVerbose);

		EATEST_VERIFY(solver.getCurrentStatus() == EConstraintSolverResult::Solved);
		if (printVerbose)
		{
			print(n, &solver, queenGraphData->getData());
		}
		nErrorCount += check(n, &solver, queenGraphData->getData());
	}
	return nErrorCount;
}

void NQueensSolvers::print(int n, ConstraintSolver* solver, const vector<VarID>& vars)
{
	for (int row = 0; row < n; ++row)
	{
		wstring rowS;
		for (int col = 0; col < n; ++col)
		{
			if (solver->getSolvedValue(vars[row]) == col)
			{
				rowS += TEXT("[Q]");
			}
			else
			{
				rowS += TEXT("[ ]");
			}
		}
		CS_LOG("%s", rowS.c_str());
	}
}

int NQueensSolvers::check(int n, ConstraintSolver* solver, const vector<VarID>& vars)
{
	int nErrorCount = 0;
	for (int row = 0; row < n; ++row)
	{
		int col = solver->getSolvedValue(vars[row]);
		int diagLeft = col - 1;
		int diagRight = col + 1;
		for (int prevRow = row - 1; prevRow >= 0; --prevRow)
		{
			int v = solver->getSolvedValue(vars[prevRow]);
			EATEST_VERIFY(v != col && v != diagLeft && v != diagRight);
			diagLeft--;
			diagRight++;
		}

		diagLeft = col - 1;
		diagRight = col + 1;
		for (int nextRow = row + 1; nextRow < n; ++nextRow)
		{
			int v = solver->getSolvedValue(vars[nextRow]);
			EATEST_VERIFY(v != col && v != diagLeft && v != diagRight);
			diagLeft--;
			diagRight++;
		}
	}

	return nErrorCount;
}
