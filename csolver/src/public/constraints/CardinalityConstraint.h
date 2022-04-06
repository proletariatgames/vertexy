// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once

#include <EASTL/hash_map.h>
#include <EASTL/functional.h>

#include "ConstraintTypes.h"
#include "topology/BipartiteGraph.h"
#include "ds/HallIntervalPropagation.h"
#include "IBacktrackingSolverConstraint.h"
#include "MaxOccurrenceExplainer.h"
#include "ds/DisjointSet.h"
#include "ds/SparseSet.h"
#include "topology/algo/tarjan.h"

namespace csolver
{

// Given a set of variables and a map from Value -> (Min,Max), ensure that the count of variables assigned to Value is
// between (Min, Max), for every value  in the map.
//
// Note if min = 0 and max = 1 for every value, the constraint is equivalent to AllDifferent, which should be
// used instead.
class CardinalityConstraint : public IBacktrackingSolverConstraint
{
public:
	CardinalityConstraint(const ConstraintFactoryParams& params, const vector<VarID>& variables, const vector<int>& mins, const vector<int>& maxs);

	struct CardinalityConstraintFactory
	{
		static CardinalityConstraint* construct(const ConstraintFactoryParams& params, const vector<VarID>& vars, const hash_map<int, tuple<int, int>>& cardinalitiesPerValue);
	};

	using Factory = CardinalityConstraintFactory;

	virtual EConstraintType getConstraintType() const override { return EConstraintType::Cardinality; }
	virtual vector<VarID> getConstrainingVariables() const override { return m_allVariables; }
	virtual bool initialize(IVariableDatabase* db) override;
	virtual void reset(IVariableDatabase* db) override;
	virtual bool onVariableNarrowed(IVariableDatabase* db, VarID variable, const ValueSet& previousValue, bool& removeWatch) override;
	virtual void backtrack(const IVariableDatabase* db, SolverDecisionLevel level) override;
	virtual bool propagate(IVariableDatabase* db) override;
	virtual bool checkConflicting(IVariableDatabase* db) const override;
	virtual bool explainConflict(const IVariableDatabase* db, vector<Literal>& outClauses) const override;

protected:
	using Interval = HallIntervalPropagation::Interval;

	bool processUpperboundConstraint(IVariableDatabase* db);
	bool processLowerboundConstraint(IVariableDatabase* db);
	bool lbcLow(IVariableDatabase* db, vector<Interval>& intervals);
	bool lbcHi(IVariableDatabase* db, vector<Interval>& intervals);
	vector<Literal> explainLowerBoundPropagation(const NarrowingExplanationParams& params) const;

	bool getMaximalMatching(IVariableDatabase* db);
	bool processChangedSCC(IVariableDatabase* db, int scc);
	template <typename T>
	void tarjanVisit(IVariableDatabase* db, int node, T&& visitor);

	bool isUpperBoundFullySatisfied(IVariableDatabase* db) const;
	bool isLowerBoundFullySatisfied(IVariableDatabase* db) const;

	// All variables involved in the constraint
	vector<VarID> m_allVariables;
	// Variables that are subject to the upper bound constraint
	vector<VarID> m_upperBoundVariables;
	// Variables that are subject to the lower bound constraint
	vector<VarID> m_lowerBoundVariables;

	hash_map<VarID, WatcherHandle> m_watcherHandles;
	// For each value, the minimum number of occurrences required for that value. Size=MaxDomainSize.
	vector<int> m_mins;
	// For each value, the maximum number of occurrences required for that value. Size=MaxDomainSize.
	vector<int> m_maxs;
	// The Mins array, but trimmed to MinDomainValue/MaxDomainValue.
	vector<int> m_trimmedMins;
	// The Maxs array, but trimmed to MinDomainValue/MaxDomainValue.
	vector<int> m_trimmedMaxs;
	// Whether any values have an upper bound constraint
	bool m_hasUpperBoundConstraint = false;
	// The values that have an upper bound constraint
	ValueSet m_upperBoundConstrainedValues;
	// Whether any values have a lower bound constraint
	bool m_hasLowerBoundConstraint = false;
	// The values that have a lower bound constraint
	ValueSet m_lowerBoundConstrainedValues;
	// Maximum domain of any variable in Variables
	int m_maxDomainSize = 0;
	// Minimum value of all variables at initialization time.
	int m_minDomainValue = 0;
	// Maximum value of all variables at initialization time.
	int m_maxDomainValue = 0;
	// The sum of all minimum counts in the lower bound constraint
	int m_lbcTotalOccurrenceSum = 0;

	// Working data: the bounds of each interval
	vector<Interval> m_bounds;
	// Working data: stores the list of variable indices that were triggered. Used in Propagate as the set
	// of variables that need enforcement.
	vector<int> m_upperBoundProcessList;

	//
	// Maximal Matching
	//

	// Working data: For each value in (MinDomainValue-MaxDomainValue), how many times it appears as a potential
	// value in each variable.
	vector<int> m_valueToSumInMatching;
	// Working data: the variable->value graph used to find the maximal matching.
	BipartiteGraph m_matchingGraph;
	// Whether we failed to find a matching last time we tried. Reset on backtrack.
	bool m_failedUpperBoundMatching = false;

	//
	// Strongly-connected components (SCCs)
	//

	// The set of nodes in each SCC. If SCCSplits[i] is 1, then SCCToNode[i+1] represents the start of a new SCC.
	vector<int> m_sccToNode;
	// Lookup from node index to index in SCCToNode.
	vector<int> m_nodeToScc;
	// Set of indices in SCCToNode that represent the end of one SCC and the beginning of another.
	// This data is trailed and restored on backtrack.
	TSparseSet<int> m_sccSplits;
	// Working data: during propagation, for the SCCs that were modified, the variables that were in the SCC.
	// Note values in VarIndices are indices in Variables array of the variable.
	vector<int> m_varIndicesInOldScc;
	// The tarjan algorithm and working data for computing SCCs
	TarjanAlgorithm m_tarjan;
	// The next index in SCCToNode to write when new SCCs are found.
	int m_tarjanNextScc = 0;
	// Whether a SCC split has been found. After this point, remaining SCCs found must be written.
	bool m_tarjanFoundSccSplit = false;
	// Number of variable nodes reached during each iteration of TarjanRecurse
	int m_tarjanVarsReached = 0;
	// Number of value nodes reached during each iteration of TarjanRecurse
	int m_tarjanValsReached = 0;
	// Whether we failed to narrow a variable during TarjanRecurse
	bool m_tarjanRecurseFailure = false;
	// For each node index >= Variables.Num(), the value corresponding to the node.
	vector<int> m_nodeIndexToActualValue;
	// For each value between MinDomainSize-MaxDomainSize, the index of the first node for that value.
	vector<int> m_trimmedValueToNodeIndex;
	// For each variable node in the matching, the node index for the corresponding matched value
	vector<int> m_variableNodeToMatchedNode;
	// For each value node in the matching, the node index of the corresponding matched variable (or -1)
	vector<int> m_valueNodeToMatchedNode;
	// The number of value nodes. Every value that has a relevant Max is duplicated Max times.
	// Other values that do not have a Max value are only included once.
	int m_numValueNodes = 0;
	// Number of SCCs that only have one variable in them. Once all SCCs have one variable, we're fully satisfied.
	int m_numUnitSCCs = 0;
	// Number of variables that are no longer affected by the UBC
	int m_numUpperBoundVarsOutsideUBC = 0;

	//
	// Lower-bound constraint working data
	//

	vector<VarID> m_lbcVars;
	vector<int> m_bucketCapacities;
	DisjointSet m_lbcPotentials;
	DisjointSet m_lbcStable;
	ValueSet m_lbcFailures;
	vector<int> m_lbcBoundaries;
	bool m_failedLowerBoundMatching = false;

	inline bool isVariableNode(int node) const { return node < m_upperBoundVariables.size(); }
	inline bool isValueNode(int node) const { return node >= m_upperBoundVariables.size() && !isSinkNode(node); }
	inline bool isSinkNode(int node) const { return node == m_upperBoundVariables.size() + m_numValueNodes; }

	inline int variableNodeToVariableIndex(int n) const
	{
		cs_sanity(isVariableNode(n));
		return n;
	}

	inline int variableIndexToNodeIndex(int v) const { return v; }

	inline int valueToFirstValueNode(int v) const
	{
		return m_trimmedValueToNodeIndex[v - m_minDomainValue];
	}

	inline int valueNodeToValue(int n) const
	{
		cs_sanity(isValueNode(n));
		return m_nodeIndexToActualValue[n - m_upperBoundVariables.size()];
	}

	mutable MaxOccurrenceExplainer m_upperBoundExplainer;

	//
	// Backtracking
	//

	struct BacktrackInfo
	{
		SolverDecisionLevel level;
		// Used to restore SCCSplits upon backtracking.
		int sccSplitcount;
		// Variable indices that will be dirty after backtracking.
		vector<int> upperBoundProcessList;
		// How many UBC variables are no longer possibly part of UBC
		int numUBCVariablesRemoved;
		// How many UBC SCCs were single variable
		int numUnitSCCs;
	};

	BacktrackInfo& backtrackRecord(SolverDecisionLevel level);

	// The level at which we became fully satisfied
	int m_fullySatisfiedLevel = -1;
	vector<BacktrackInfo> m_backtrackStack;
};

} // namespace csolver