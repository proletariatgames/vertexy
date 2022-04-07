// Copyright Proletariat, Inc. All Rights Reserved.
#include "constraints/ConstraintGraphRelationInfo.h"
#include "topology/GraphRelations.h"

using namespace Vertexy;

ConstraintGraphRelationInfo::ConstraintGraphRelationInfo()
	: sourceGraphVertex(-1)
{
}

ConstraintGraphRelationInfo::ConstraintGraphRelationInfo(const shared_ptr<ITopology>& graph, int sourceVertex)
	: graph(graph)
	, sourceGraphVertex(sourceVertex)
{
}

void ConstraintGraphRelationInfo::clear()
{
	graph = nullptr;
	sourceGraphVertex = -1;
	isValid = false;
	relations.clear();
}

void ConstraintGraphRelationInfo::reset(const shared_ptr<ITopology>& sourceGraph, int sourceVertex)
{
	graph = sourceGraph;
	sourceGraphVertex = sourceVertex;
	isValid = true;
	relations.clear();
}

void ConstraintGraphRelationInfo::addRelation(VarID var, const ConstraintGraphRelation& relation)
{
	vxy_assert(!visit([](auto&& typed) { return typed == nullptr; }, relation));

	auto it = find_if(relations.begin(), relations.end(), [&](auto&& entry) { return entry.var == var; });
	if (it != relations.end())
	{
		VERTEXY_WARN("Variable %d is being referred to be two separate relations in the same constraint. This will prevent it from being used for graph learning.", var.raw());
		isValid = false;
		return;
	}
	relations.push_back({var, relation});
}

bool ConstraintGraphRelationInfo::getRelation(VarID var, ConstraintGraphRelation& outRelation) const
{
	auto it = find_if(relations.begin(), relations.end(), [&](auto&& entry) { return entry.var == var; });
	if (it == relations.end())
	{
		return false;
	}
	outRelation = it->relation;
	return true;
}
