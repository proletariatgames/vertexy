// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once
#include "ConstraintTypes.h"
#include "TopologySearchConstraint.h"
#include "IBacktrackingSolverConstraint.h"
#include "ISolverConstraint.h"
#include "SignedClause.h"
#include "ds/ESTree.h"
#include "ds/RamalReps.h"
#include "topology/BacktrackingDigraphTopology.h"
#include "topology/DigraphEdgeTopology.h"
#include "topology/TopologyVertexData.h"
#include "topology/algo/MaxFlowMinCut.h"
#include "variable/IVariableDatabase.h"

#define REACHABILITY_USE_RAMAL_REPS 1

namespace Vertexy
{

/** Constraint to ensure reachability in a graph from a set of sources.
 *  The SourceGraph specifies the full potential connectivity set of all variables.
 *  Any variable that is definitely one of SourceValues is considered a reachability source.
 *  Any variable that is definitely on of NeedReachableValues is considered a destination that must remain reachable from AT LEAST one source.
 */
class ReachabilityConstraint : public ITopologySearchConstraint
{
public:
	ReachabilityConstraint(const ConstraintFactoryParams& params,
		const shared_ptr<TTopologyVertexData<VarID>>& sourceGraphData,
		const ValueSet& sourceMask,
		const ValueSet& requireReachableMask,
		const shared_ptr<TTopologyVertexData<VarID>>& edgeGraphData,
		const ValueSet& edgeBlockedMask
	);

	struct ReachabilityFactory
	{
		static ReachabilityConstraint* construct(
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
			const vector<int>& edgeBlockedValues);
	};

	using Factory = ReachabilityFactory;

	virtual EConstraintType getConstraintType() const override { return EConstraintType::Reachability; }

protected:

	virtual bool isValidDistance(int dist) const override;
	
};

} // namespace Vertexy