// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once

#include "ConstraintTypes.h"
#include "SignedClause.h"
#include <EASTL/variant.h>

namespace Vertexy
{

template <typename T>
class IGraphRelation;

template <typename T>
class TTopologyVertexData;

class ITopology;

using ConstraintGraphRelation = variant<
	shared_ptr<const IGraphRelation<VarID>>,
	shared_ptr<const IGraphRelation<SignedClause>>,
	shared_ptr<const IGraphRelation<Literal>>
>;

/** Stores information about the graph relationships between variables in a constraint */
class ConstraintGraphRelationInfo
{
public:
	ConstraintGraphRelationInfo();
	ConstraintGraphRelationInfo(const shared_ptr<ITopology>& graph, int sourceVertex);

	void clear();
	void reset(const shared_ptr<ITopology>& graph, int sourceVertex);

	void reserve(int numRelations)
	{
		relations.reserve(numRelations);
	}

	void addRelation(VarID var, const ConstraintGraphRelation& relation);
	bool getRelation(VarID var, ConstraintGraphRelation& outRelation) const;

	// Graph this constraint is associated with.
	shared_ptr<ITopology> graph;
	// The vertex within the graph this constraint was instantiated for.
	int sourceGraphVertex;

	struct VariableRelation
	{
		VarID var;
		ConstraintGraphRelation relation;
	};

	vector<VariableRelation> relations;

	// Whether this is valid. Currently we treat the constraint as non-graph if there are multiple relations
	// to the same variable within the same constraint.
	bool isValid = true;
};

} // namespace Vertexy