// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once

#include "ConstraintTypes.h"
#include "IBacktrackingSolverConstraint.h"

#include "ISolverConstraint.h"
#include "ds/SparseSet.h"
#include "variable/SolverVariableDomain.h"

namespace Vertexy
{
// Data for a given set of tuples, once the tuples have been established to be arc-consistent.
struct TableConstraintIntermediateData
{
	TableConstraintIntermediateData(const vector<vector<int>>& inTupleRows)
		: tupleRows(inTupleRows)
	{
	}

	// Build the supports table, if not built already. The input variables are just used to grab the domains.
	void buildSupportsIfNeeded(const IVariableDatabase* db, const vector<VarID>& exampleVariables);

	// Valid tuples. Copied from ConstraintData and adjusted to remove any invalid tuples once initial arc consistency pass has finished.
	const vector<vector<int>>& tupleRows;

	// The support tables. For each variable, for each value, the indices of each tuple in TupleRows that contain that value for that variable.
	// [VariableIndex][ValueIndex][RowIndex]
	vector<vector<vector<int>>> supports;
};

// Static/immutable data for defining a table constraint
struct TableConstraintData
{
	// List of allowable tuples for this constraint. Each entry is a list of values, one per variable involved in the constraint.
	// It is assumed that each value is within the domain of the corresponding variable.
	vector<vector<int>> tupleRows;

	TableConstraintData()
	{
	}

	TableConstraintData(const vector<vector<int>>& inRows)
		: tupleRows(inRows)
	{
	}

	// Crete a new set of tuples by converting the domains (which may not be zero-based) into zero-based
	shared_ptr<TableConstraintData> convertFromDomains(const vector<SolverVariableDomain>& domains) const;

	void setData(const vector<vector<int>>& inRows);

	// Returns the intermediate data for this set of tuples. Calculated on-demand.
	const shared_ptr<TableConstraintIntermediateData>& getIntermediateData() const;

	void clear();

	inline bool isValid() const { return tupleRows.size() > 0; }

private:
	mutable shared_ptr<TableConstraintIntermediateData> m_intermediateData;
};

using TableConstraintDataPtr = shared_ptr<TableConstraintData>;

// Constraint that, given a list of variables, and a table of allowed value combinations for each variable, ensures
// that one of the rows in the table is selected.
//
// Uses the Str3 algorithm for maintaining arc consistency.
// See "A Path-Optimal GAC Algorithm for Table Constraints" Lecoutre et. al.
// https://citeseerx.ist.psu.edu/viewdoc/download?doi=10.1.1.455.6489&rep=rep1&type=pdf
//
class TableConstraint : public IBacktrackingSolverConstraint
{
protected:
	// Stores data to allow for restoration of constraints when backtracking.
	struct BacktrackData
	{
		BacktrackData(int inLevel, int inNumInvalidatedRows)
			: level(inLevel)
			, numInvalidatedRows(inNumInvalidatedRows)
		{
		}

		// The solver step this backtrack data corresponds to.
		SolverDecisionLevel level;

		// Number of invalidated rows prior to this step
		int numInvalidatedRows;

		// Maps (Variable,Value) -> Cursor
		hash_map<tuple<int, int>, int> cursors;
	};

public:
	TableConstraint(const ConstraintFactoryParams& params, const TableConstraintDataPtr& inData, const vector<VarID>& inVariables);

	struct TableConstraintFactory
	{
		static TableConstraint* construct(const ConstraintFactoryParams& params, const shared_ptr<struct TableConstraintData>& data, const vector<VarID>& variables);
	};

	using Factory = TableConstraintFactory;

	virtual EConstraintType getConstraintType() const override { return EConstraintType::Table; }
	virtual vector<VarID> getConstrainingVariables() const override { return m_variables; }
	virtual bool initialize(IVariableDatabase* db) override;
	virtual void reset(IVariableDatabase* db) override;
	virtual void onInitialArcConsistency(IVariableDatabase* db) override;
	virtual bool onVariableNarrowed(IVariableDatabase* db, VarID variable, const ValueSet& previousValue, bool& removeWatch) override;
	virtual bool checkConflicting(IVariableDatabase* db) const override;
	virtual void backtrack(const IVariableDatabase* db, SolverDecisionLevel level) override;

protected:
	BacktrackData& getOrCreateBacktrackData(SolverDecisionLevel level, int prevNumInvalidatedRows);

	// Reference to allowed row data.
	TableConstraintDataPtr m_constraintData;

	// The variables for this instance of the constraint.
	vector<VarID> m_variables;
	// Watch for each variable
	vector<WatcherHandle> m_watchers;

	// For each support table, the cursor for each value entry. Row entries beyond this cursor are known to be invalid.
	vector<vector<int>> m_rowCursors;

	// State for backtracking
	vector<BacktrackData> m_backtrackStack;

	vector<vector<int>> m_instancedTupleRows;
	shared_ptr<TableConstraintIntermediateData> m_intermediateData;

	// The indices in ConstraintData.TupleRows that are currently invalid.
	TSparseSet<int> m_invalidatedRows;

	// For each row, a set of [variable, value] that depend on that row for support.
	vector<hash_set<tuple<int, int>>> m_dependencies;
};

} // namespace Vertexy