// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once

#include "ConstraintTypes.h"
#include "constraints/IConstraint.h"
#include "constraints/ClauseConstraint.h"
#include <EASTL/variant.h>

namespace Vertexy
{
class ClauseConstraint;

template <typename T>
class TManyToOneGraphRelation;

/** Class responsible for finding explanations for conflicts reached during search, and returning an explanation
 *  and decision level that will avoid the conflict.
 */
class ConflictAnalyzer
{
public:
	explicit ConflictAnalyzer(ConstraintSolver& inSolver);

	// Analyze the conflict and return the learned constraint and backtrack level
	SolverDecisionLevel analyzeConflict(SolverTimestamp conflictTs, IConstraint* conflictingConstraint, VarID contradictingVariable, ClauseConstraint*& outLearned);

protected:
	// Used during conflict analysis to analyze the implication graph in order to find an explanation for conflict.
	struct ImplicationNode
	{
		ImplicationNode()
			: var(VarID::INVALID)
			, time(-1)
			, level(-1)
		{
		}

		ImplicationNode(VarID var, SolverTimestamp time, SolverDecisionLevel level)
			: var(var)
			, time(time)
			, level(level)
		{
		}

		void clearGraphRelation()
		{
			relation = ConstraintGraphRelation();
		}

		inline bool hasGraphRelation() const
		{
			return visit([](auto&& typedRel) { return typedRel != nullptr; }, relation);
		}

		VarID var;
		SolverTimestamp time;
		SolverDecisionLevel level;
		shared_ptr<TManyToOneGraphRelation<VarID>> multiRelation;
		ConstraintGraphRelation relation;
	};

	SolverDecisionLevel searchImplicationGraph(vector<Literal>& explanation, const IConstraint* initialConflict, int conflictTime);

	void markActivity(const vector<Literal>& resolvedExplanation, SolverTimestamp uipTime);

	void resolve(const vector<Literal>& newClauses, const ConstraintGraphRelationInfo& relationInfo, vector<Literal>& outClauses, VarID pivotVar, SolverTimestamp newClauseTimestamp);
	bool relax(ImplicationNode& node, const ValueSet& assertingValue);
	void insertClause(const Literal& clause, const ConstraintGraphRelationInfo& originRelationInfo, vector<Literal>& outClauses, SolverTimestamp newTimestamp);

	enum class EGraphRelationType : uint8_t
	{
		Initialize,
		Union,
		Intersection
	};

	using ClauseRelationType = shared_ptr<const IGraphRelation<SignedClause>>;
	using LiteralRelationType = shared_ptr<const IGraphRelation<Literal>>;
	using VariableRelationType = shared_ptr<const IGraphRelation<VarID>>;

	void applyGraphRelation(ImplicationNode& node, const ConstraintGraphRelationInfo& originGraphInfo, const ValueSet& values, EGraphRelationType applicationType);

	int findMostRecentNodeIndex() const;
	static bool compatibleRelations(const ConstraintGraphRelation& existingRelation, const ConstraintGraphRelation& newRelation);

	static bool isClauseRelation(const ConstraintGraphRelation& rel)
	{
		return visit([&](auto&& typedRel)
		{
			vxy_assert(typedRel != nullptr);
			using T = decay_t<decltype(typedRel)>;
			return is_same_v<T, ClauseRelationType> || is_same_v<T, LiteralRelationType>;
		}, rel);
	}

	template <typename T>
	shared_ptr<const IGraphRelation<T>> createOffsetGraphRelation(int graphNode, const shared_ptr<const IGraphRelation<T>>& inRel);

	SolverTimestamp findLatestFalseTime(VarID var, const ValueSet& assertingValue, SolverTimestamp latestTime) const;

	int getNodeIndexForVar(VarID var) const;
	void refreshTopLevel();
	void refreshTopLevelNodeCount();

	inline static uint32_t getLevelBit(SolverDecisionLevel level) { return 1 << (level & 31); }
	bool checkRedundant(const vector<Literal>& explanation, int litIndex, uint32_t levelMask) const;

	ConstraintSolver& m_solver;

	SolverDecisionLevel m_topLevel = -1;
	int m_numTopLevelNodes = -1;
	shared_ptr<ITopology> m_graph = nullptr;
	int m_anchorGraphVertex = -1;

	vector<ImplicationNode> m_nodes;
	unique_ptr<ConstraintGraphRelationInfo> m_resolvedRelationInfo;
	vector<int> m_variableClauseIndices;

	mutable vector<ImplicationNode> m_redundancyStack;
	mutable vector<ValueSet> m_redundancyValues;
	mutable ValueSet m_redundancySeen;
};

} // namespace Vertexy