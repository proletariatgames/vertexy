// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once
#include "ConstraintTypes.h"
#include "TopologySearchConstraint.h"
#include "IConstraint.h"
#include "SignedClause.h"
#include "ds/ESTree.h"
#include "ds/RamalReps.h"
#include "topology/BacktrackingDigraphTopology.h"
#include "topology/DigraphEdgeTopology.h"
#include "topology/TopologyVertexData.h"
#include "topology/algo/MaxFlowMinCut.h"
#include "topology/algo/ShortestPath.h"
#include "variable/IVariableDatabase.h"
#include "constraints/ConstraintOperator.h"
#include "ds/BacktrackableValue.h"

#define REACHABILITY_USE_RAMAL_REPS 1

namespace Vertexy
{

/** Constraint to ensure the shortest paths between source and destination nodes
 *  All destination nodes must have at least 1 shortest path to at least 1 source node
 *  this shortest path must have a relation with 'distance'
 */
class ShortestPathConstraint : public ITopologySearchConstraint
{
public:
	ShortestPathConstraint(const ConstraintFactoryParams& params,
		const shared_ptr<TTopologyVertexData<VarID>>& sourceGraphData,
		const ValueSet& sourceMask,
		const ValueSet& requireReachableMask,
		const shared_ptr<TTopologyVertexData<VarID>>& edgeGraphData,
		const ValueSet& edgeBlockedMask,
		EConstraintOperator op,
		VarID distance
	);

	struct ShortestPathFactory
	{
		static ShortestPathConstraint* construct(
			const ConstraintFactoryParams& params,
			// Graph of vertices where reachability is calculated
			const shared_ptr<TTopologyVertexData<VarID>>& sourceGraphData,
			// Values of vertices in SourceGraph that establish it as a reachability source
			const vector<int>& sourceValues,
			// Values of vertices in SourceGraph that establish it as needing to be reachable from a source
			const vector<int>& needReachableValues,
			// The variables for each edge of source graph
			const shared_ptr<TTopologyVertexData<VarID>>& edgeGraphData,
			// Values of vertices in the edge graph establishing that edge as "off"
			const vector<int>& edgeBlockedValues,
			// How to compare distance
			EConstraintOperator op,
			// The variable that stores the distance
			VarID distance);
	};

	using Factory = ShortestPathFactory;

	virtual EConstraintType getConstraintType() const override { return EConstraintType::ShortestPath; }
	virtual void onInitialArcConsistency(IVariableDatabase* db) override;

protected:

	bool isValidDistance(const IVariableDatabase* db, int dist) const;

	virtual bool isPossiblyValid(const IVariableDatabase* db, const ReachabilitySourceData& src, int vertex) override;
	virtual EValidityDetermination determineValidityHelper(const IVariableDatabase* db, const ReachabilitySourceData& src, int vertex, VarID srcVertex) override;
	virtual shared_ptr<RamalRepsType> makeTopology(const shared_ptr<BacktrackingDigraphTopology>& graph) const override;
	virtual EventListenerHandle addMinCallback(RamalRepsType& minReachable, const IVariableDatabase* db, VarID source) override;
	virtual EventListenerHandle addMaxCallback(RamalRepsType& maxReachable, const IVariableDatabase* db, VarID source) override;
	virtual vector<Literal> explainInvalid(const NarrowingExplanationParams& params) override;
	virtual void createTempSourceData(ReachabilitySourceData& data, int vertexIndex) const override;
	void onVertexChanged(int vertexIndex, VarID sourceVar, bool inMinGraph);
	void backtrack(const IVariableDatabase* db, SolverDecisionLevel level) override;

	EConstraintOperator m_op;
	VarID m_distance;

	// used to determine the path that is either too long or too short when explaning
	ShortestPathAlgorithm m_shortestPathAlgo;

	// used when m_op is LessThan/LessThanEq and records the last timestamp in which a vertex was deemed valid
	hash_map<int, hash_map<int, TBacktrackableValue<SolverTimestamp>>> m_lastValidTimestamp;
	hash_map<int, hash_map<int, SolverTimestamp>> m_validTimestampsToCommit;
	hash_set<tuple<int, int>> m_alwaysForbiddenPaths;

	hash_set<int> m_queuedVertexChanges;
	bool m_processingVertices = false;

	void commitValidTimestamps(const IVariableDatabase* db);
	void onEdgeChangeFailure(const IVariableDatabase* db) override;
	void onEdgeChangeSuccess(const IVariableDatabase* db) override;
	void addTimestampToCommit(const IVariableDatabase* db, int sourceVertex, int destVertex);
	void removeTimestampToCommit(const IVariableDatabase* db, int sourceVertex, int destVertex);
	void processQueuedVertexChanges(IVariableDatabase* db) override;
};

} // namespace Vertexy