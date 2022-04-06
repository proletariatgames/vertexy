// Copyright Proletariat, Inc. All Rights Reserved.
#include "constraints/ConstraintGraphRelationInfo.h"
#include "topology/GraphRelations.h"

using namespace csolver;

ConstraintGraphRelationInfo::ConstraintGraphRelationInfo()
	: sourceGraphNode(-1)
{
}

ConstraintGraphRelationInfo::ConstraintGraphRelationInfo(const shared_ptr<ITopology>& graph, int sourceNode)
	: graph(graph)
	, sourceGraphNode(sourceNode)
{
}

void ConstraintGraphRelationInfo::clear()
{
	graph = nullptr;
	sourceGraphNode = -1;
	isValid = false;
	relations.clear();
}

void ConstraintGraphRelationInfo::reset(const shared_ptr<ITopology>& sourceGraph, int sourceNode)
{
	graph = sourceGraph;
	sourceGraphNode = sourceNode;
	isValid = true;
	relations.clear();
}

void ConstraintGraphRelationInfo::addRelation(VarID var, const ConstraintGraphRelation& relation)
{
	cs_assert(!visit([](auto&& typed) { return typed == nullptr; }, relation));

	auto it = find_if(relations.begin(), relations.end(), [&](auto&& entry) { return entry.var == var; });
	if (it != relations.end())
	{
		CS_WARN("Variable %d is being referred to be two separate relations in the same constraint. This will prevent it from being used for graph learning.", var.raw());
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
