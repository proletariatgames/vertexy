// Copyright Proletariat, Inc. All Rights Reserved.

#include "constraints/TableConstraint.h"
#include "variable/IVariableDatabase.h"

using namespace csolver;

void TableConstraintIntermediateData::buildSupportsIfNeeded(const IVariableDatabase* db, const vector<VarID>& exampleVariables)
{
	if (!supports.empty())
	{
		return;
	}

	const int numVariables = tupleRows[0].size();

	//
	// Create the supports map: For each variable, for each potential value, the rows that contain that value for that variable.
	//

	supports.clear();
	supports.resize(numVariables);

	for (int rowIndex = 0; rowIndex < tupleRows.size(); ++rowIndex)
	{
		const vector<int>& row = tupleRows[rowIndex];
		cs_assert(row.size() == numVariables);

		for (int variableIndex = 0; variableIndex < row.size(); ++variableIndex)
		{
			vector<vector<int>>& variableSupports = supports[variableIndex];

			const int variableValue = row[variableIndex];

			variableSupports.resize(db->getDomainSize(exampleVariables[variableIndex]));
			if (variableValue >= 0 && variableValue < variableSupports.size())
			{
				variableSupports[variableValue].push_back(rowIndex);
			}
		}
	}
}

void TableConstraintData::clear()
{
	tupleRows.clear();
	m_intermediateData.reset();
}

void TableConstraintData::setData(const vector<vector<int>>& inRows)
{
	tupleRows = inRows;
	m_intermediateData.reset();
}

TableConstraintDataPtr TableConstraintData::convertFromDomains(const vector<SolverVariableDomain>& domains) const
{
	vector<vector<int>> convertedRows;
	convertedRows.reserve(tupleRows.size());
	for (const vector<int>& row : tupleRows)
	{
		vector<int> newRow;
		newRow.reserve(row.size());
		for (int varIndex = 0; varIndex < row.size(); ++varIndex)
		{
			newRow.push_back(domains[varIndex].getIndexForValue(row[varIndex]));
		}
		convertedRows.push_back(move(newRow));
	}
	return make_shared<TableConstraintData>(convertedRows);
}

const shared_ptr<TableConstraintIntermediateData>& TableConstraintData::getIntermediateData() const
{
	if (m_intermediateData.get() == nullptr)
	{
		auto iData = make_shared<TableConstraintIntermediateData>(tupleRows);
		m_intermediateData = iData;
	}
	return m_intermediateData;
}

TableConstraint* TableConstraint::TableConstraintFactory::construct(const ConstraintFactoryParams& params, const shared_ptr<TableConstraintData>& data, const vector<VarID>& variables)
{
	TableConstraintDataPtr convertedData = data;
	auto found = find_if(variables.begin(), variables.end(), [&](VarID v)
	{
		return params.getDomain(v).getMin() != 0;
	});

	if (found != variables.end())
	{
		vector<SolverVariableDomain> domains;
		domains.reserve(variables.size());
		for (VarID varId : variables)
		{
			domains.push_back(params.getDomain(varId));
		}
		convertedData = data->convertFromDomains(domains);
	}

	return new TableConstraint(params, convertedData, variables);
}

TableConstraint::TableConstraint(const ConstraintFactoryParams& params, const TableConstraintDataPtr& inData, const vector<VarID>& inVariables)
	: IBacktrackingSolverConstraint(params)
	, m_constraintData(inData)
	, m_variables(inVariables)
{
}

bool TableConstraint::initialize(IVariableDatabase* db)
{
	//
	// For each variable, find the total set of allowable values
	//

	vector<ValueSet> allowableValues;
	allowableValues.resize(m_variables.size());

	for (int tupleIndex = 0; tupleIndex < m_constraintData->tupleRows.size(); ++tupleIndex)
	{
		for (int variableIndex = 0; variableIndex < m_variables.size(); ++variableIndex)
		{
			const int variableValue = m_constraintData->tupleRows[tupleIndex][variableIndex];

			allowableValues[variableIndex].pad(db->getDomainSize(m_variables[variableIndex]), false);
			allowableValues[variableIndex][variableValue] = true;
		}
	}

	//
	// Constrain each variable to exclude values that aren't allowed
	//

	for (int i = 0; i < m_variables.size(); ++i)
	{
		m_watchers.push_back(db->addVariableWatch(m_variables[i], EVariableWatchType::WatchModification, this));
		if (!db->constrainToValues(m_variables[i], allowableValues[i], this))
		{
			return false;
		}
	}

	return true;
}

void TableConstraint::reset(IVariableDatabase* db)
{
	m_intermediateData.reset();
	m_rowCursors.clear();
	m_invalidatedRows.clear();
	m_dependencies.clear();
	m_backtrackStack.clear();

	for (int i = 0; i < m_watchers.size(); ++i)
	{
		db->removeVariableWatch(m_variables[i], m_watchers[i], this);
	}
	m_watchers.clear();
}

bool TableConstraint::onVariableNarrowed(IVariableDatabase* db, VarID variable, const ValueSet& prevValues, bool&)
{
	int variableIndex = indexOf(m_variables.begin(), m_variables.end(), variable);
	cs_assert(variableIndex >= 0);

	if (!db->hasFinishedInitialArcConsistency())
	{
		// We haven't yet finished the initial application of constraints, so we can't use Str3 for support resolution.
		// For now, we just do a naive implementation. Constrain the other variables based on what values the narrowed variable can be.
		vector<ValueSet> allowableValuesPerVariable;
		allowableValuesPerVariable.resize(m_variables.size());

		for (const vector<int>& rowTuple : m_constraintData->tupleRows)
		{
			if (db->isPossible(variable, rowTuple[variableIndex]))
			{
				for (int depVarIndex = 0; depVarIndex != rowTuple.size(); ++depVarIndex)
				{
					if (depVarIndex == variableIndex)
					{
						continue;
					}
					const VarID depVariable = m_variables[depVarIndex];
					const int depVarValue = rowTuple[depVarIndex];

					allowableValuesPerVariable[depVarIndex].pad(db->getDomainSize(depVariable), false);
					allowableValuesPerVariable[depVarIndex][depVarValue] = true;
				}
			}
		}

		// Constrain all other dependent variables (excluding the narrowed variable)
		for (int i = 0; i < m_variables.size(); ++i)
		{
			if (i == variableIndex)
			{
				continue;
			}

			if (!db->constrainToValues(m_variables[i], allowableValuesPerVariable[i], this))
			{
				return false;
			}
		}
	}
	else
	{
		const ValueSet& curValues = db->getPotentialValues(variable);

		const int prevMembers = m_invalidatedRows.size();
		for (auto it = prevValues.beginSetBits(), itEnd = prevValues.endSetBits(); it != itEnd; ++it)
		{
			const int removedValue = *it;
			if (curValues[removedValue])
			{
				continue;
			}

			cs_assert(!m_rowCursors.empty());
			cs_assert(removedValue >= 0 && removedValue < m_rowCursors[variableIndex].size());

			int cursor = m_rowCursors[variableIndex][removedValue];

			// Add all tuple rows that contain the removed value to the invalidated constraint list.
			const vector<int>& valueSupportRows = m_intermediateData->supports[variableIndex][removedValue];
			for (int k = 0; k <= cursor; ++k)
			{
				m_invalidatedRows.add(valueSupportRows[k]);
			}
		}

		if (m_invalidatedRows.size() == prevMembers)
		{
			// Nothing changed
			return true;
		}

		BacktrackData& backtrackData = getOrCreateBacktrackData(db->getDecisionLevel(), prevMembers);

		// go through any newly invalidated rows, and find new supports for each value of every other variable.
		for (int i = prevMembers; i < m_invalidatedRows.size(); ++i)
		{
			const int invalidatedRowIndex = m_invalidatedRows[i];

			// TODO: Find good inline size
			fixed_vector<tuple<int, int>, 16> dependenciesToRemove;

			// Go through each variable+value tuple that currently depends on this row for support
			for (auto& depVariableAndValue : m_dependencies[invalidatedRowIndex])
			{
				const int depVariableIndex = get<0>(depVariableAndValue);
				const int depVariableValue = get<1>(depVariableAndValue);
				VarID depVariable = m_variables[depVariableIndex];
				if (db->isPossible(depVariable, depVariableValue))
				{
					cs_assert(variable != depVariable);

					//
					// Look (backwards) through the list of other rows that can support this variable/value. Find the
					// first row that is still supported.
					//

					const vector<int>& depSupports = m_intermediateData->supports[depVariableIndex][depVariableValue];

					// Go backwards until we find a row that hasn't already been invalidated.
					int depCursor = m_rowCursors[depVariableIndex][depVariableValue];
					while (depCursor >= 0 && m_invalidatedRows.contains(depSupports[depCursor]))
					{
						--depCursor;
					}

					// Has the dependent variable's value run out of supports (i.e. tuples that can still potentially exist)?
					if (depCursor < 0)
					{
						// Remove the value from the variable's potential set, and add any dependent constraints to the propagation queue.
						if (!db->excludeValue(depVariable, depVariableValue, this))
						{
							return false;
						}
					}
					else
					{
						// The dependent variable is still supported by some row other than the invalidated one.
						// Update the cursor, pointing it to the last-most row in the support list that is still supported.
						if (depCursor != m_rowCursors[depVariableIndex][depVariableValue])
						{
							tuple<int, int> indices(depVariableIndex, depVariableValue);
							if (backtrackData.cursors.find(indices) == backtrackData.cursors.end())
							{
								backtrackData.cursors[indices] = m_rowCursors[depVariableIndex][depVariableValue];
							}
							m_rowCursors[depVariableIndex][depVariableValue] = depCursor;
						}

						// The variable+value can no longer depend on this for a support, so we need to point it to
						// a new support. We've already calculated this (DepCursor). We don't need to store this
						// modification in backtrack data, because the support will remain valid if/when we backtrack.
						// See STR3 paper for more detailed explanation.

						dependenciesToRemove.push_back(depVariableAndValue);
						m_dependencies[depSupports[depCursor]].insert(depVariableAndValue);
					}
				}
			}

			for (auto& depToRemove : dependenciesToRemove)
			{
				m_dependencies[invalidatedRowIndex].erase(depToRemove);
			}
		}
	}

	return true;
}

void TableConstraint::backtrack(const IVariableDatabase*, SolverDecisionLevel level)
{
	while (m_backtrackStack.back().level > level)
	{
		// Restore data to what it was before this step.
		BacktrackData& restoreState = m_backtrackStack.back();
		m_invalidatedRows.backtrack(restoreState.numInvalidatedRows);
		for (auto it = restoreState.cursors.begin(), itEnd = restoreState.cursors.end(); it != itEnd; ++it)
		{
			const tuple<int, int>& variableAndValueIndices = it->first;
			m_rowCursors[get<0>(variableAndValueIndices)][get<1>(variableAndValueIndices)] = it->second;
		}
		m_backtrackStack.pop_back();
	}
}

void TableConstraint::onInitialArcConsistency(IVariableDatabase* db)
{
	//
	// Create the final set of tuples: those that are valid after initial arc consistency has been established.
	// (STR3 only works when the set of tuples is initially arc-consistent)
	//

	const int numVariables = m_variables.size();

	// First pass: check if any of the original tuples have been invalidated.
	// If so, we need to use our own tuple set and support table. Otherwise,
	// we can use a shared one (saving memory and avoiding support table recalculation).

	cs_assert(m_intermediateData.get() == nullptr);

	bool tuplesChanged = false;
	for (const vector<int>& row : m_constraintData->tupleRows)
	{
		cs_assert(row.size() == numVariables);
		int i = 0;
		auto found = find_if(m_variables.begin(), m_variables.end(), [&](VarID varID)
		{
			return !db->isPossible(varID, row[i++]);
		});

		if (found != m_variables.end())
		{
			tuplesChanged = true;
			break;
		}
	}

	if (!tuplesChanged)
	{
		m_intermediateData = m_constraintData->getIntermediateData();
	}
	else
	{
		// Some tuples are no longer valid, so we need to cull and build our own support table.
		m_instancedTupleRows.clear();
		for (const vector<int>& row : m_constraintData->tupleRows)
		{
			cs_assert(row.size() == numVariables);
			int i = 0;
			auto found = find_if(m_variables.begin(), m_variables.end(), [&](VarID v)
			{
				return !db->isPossible(v, row[i++]);
			});
			if (found != m_variables.end())
			{
				m_instancedTupleRows.push_back(row);
			}
		}

		// All potential combinations have already been ruled out.
		// This should've been caught during Initialize()
		cs_assert(m_instancedTupleRows.size() > 0);
		m_intermediateData = make_shared<TableConstraintIntermediateData>(m_instancedTupleRows);
	}

	m_intermediateData->buildSupportsIfNeeded(db, m_variables);
	m_invalidatedRows.reserve(m_intermediateData->tupleRows.size());

	//
	// Set up the cursors for each row. For each variable, for each value, the cursor is set to the last support row that is valid.
	//
	// Also create the initial dependency map: for each row, the list of (variable, value) pairs that currently
	// depend on that row for support.
	//
	// Also create the initial backtrack state - needed if we backtrack all the way to the beginning.
	//

	m_rowCursors.resize(numVariables);
	m_dependencies.clear();
	m_dependencies.resize(m_intermediateData->tupleRows.size());
	auto& backtrackData = getOrCreateBacktrackData(0, 0);

	for (int varIndex = 0; varIndex < numVariables; ++varIndex)
	{
		vector<int>& cursorsForRow = m_rowCursors[varIndex];
		VarID variable = m_variables[varIndex];
		const vector<vector<int>>& supportsForRow = m_intermediateData->supports[varIndex];
		cs_assert(supportsForRow.size() == db->getDomainSize(variable));

		cursorsForRow.resize(supportsForRow.size());
		for (int value = 0; value < supportsForRow.size(); ++value)
		{
			m_dependencies[supportsForRow[value][0]].insert(make_tuple(varIndex, value));
			cursorsForRow[value] = supportsForRow[value].size() - 1;
			backtrackData.cursors[make_tuple(varIndex, value)] = cursorsForRow[value];
		}
	}
}

TableConstraint::BacktrackData& TableConstraint::getOrCreateBacktrackData(SolverDecisionLevel level, int prevNumInvalidatedRows)
{
	if (m_backtrackStack.size() == 0 || m_backtrackStack.back().level != level)
	{
		cs_assert(m_backtrackStack.empty() || m_backtrackStack.back().level < level);
		m_backtrackStack.push_back(BacktrackData(level, prevNumInvalidatedRows));
	}
	return m_backtrackStack.back();
}

bool TableConstraint::checkConflicting(IVariableDatabase* db) const
{
	for (const vector<int>& rowTuple : m_constraintData->tupleRows)
	{
		bool possible = true;
		for (int i = 0; i < m_variables.size(); ++i)
		{
			if (!db->isPossible(m_variables[i], rowTuple[i]))
			{
				possible = false;
				break;
			}
		}

		if (possible)
		{
			return false;
		}
	}
	return true;
}
