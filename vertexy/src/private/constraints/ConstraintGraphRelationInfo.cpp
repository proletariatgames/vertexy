// Copyright Proletariat, Inc. All Rights Reserved.
#include "constraints/ConstraintGraphRelationInfo.h"
#include "topology/GraphRelations.h"

using namespace Vertexy;

ConstraintGraphRelationInfo::ConstraintGraphRelationInfo()
	: m_sourceGraphVertex(-1)
{
}

ConstraintGraphRelationInfo::ConstraintGraphRelationInfo(const shared_ptr<ITopology>& graph, int sourceVertex, const IGraphRelationPtr<bool>& filter)
	: m_filter(filter)
	, m_graph(graph)
	, m_sourceGraphVertex(sourceVertex)
{
}

void ConstraintGraphRelationInfo::invalidate()
{
	m_isValid = false;
	m_variableRelations.clear();
	m_literalRelations.clear();
}

void ConstraintGraphRelationInfo::reset(const shared_ptr<ITopology>& sourceGraph, int sourceVertex)
{
	m_graph = sourceGraph;
	m_sourceGraphVertex = sourceVertex;
	m_isValid = true;
	m_variableRelations.clear();
	m_literalRelations.clear();
}

void ConstraintGraphRelationInfo::addVariableRelation(VarID var, const IGraphRelationPtr<VarID>& relation)
{
	vxy_assert(relation != nullptr);

	auto it = find_if(m_variableRelations.begin(), m_variableRelations.end(), [&](auto&& entry)
	{
		return entry.var == var;
	});
	if (it != m_variableRelations.end())
	{
		VERTEXY_WARN("Variable %d is being referred to by multiple relations in the same constraint. This will prevent it from being used for graph learning.", var.raw());
		m_isValid = false;
		return;
	}

	auto itLit = find_if(m_literalRelations.begin(), m_literalRelations.end(), [&](auto&& entry) { return entry.lit.variable == var; });
	if (itLit != m_literalRelations.end())
	{
		VERTEXY_WARN("Variable %d is being referred to by both variable and literal relations in the same constraint. This will prevent it from being used for graph learning.", var.raw());
		m_isValid = false;
		return;
	}
	
	m_variableRelations.push_back({var, relation});
}

void ConstraintGraphRelationInfo::addLiteralRelation(const Literal& lit, const IGraphRelationPtr<Literal>& relation)
{
	vxy_assert(relation != nullptr);
	
	auto it = find_if(m_literalRelations.begin(), m_literalRelations.end(), [&](auto&& entry)
	{
		return entry.lit.variable == lit.variable && lit.values.anyPossible(entry.lit.values);
	});
	if (it != m_literalRelations.end())
	{
		vxy_sanity(it->lit.variable == lit.variable);
		if (it->lit.values == lit.values)
		{
			// Multiple relations referring to same literal. Resolve the two relations into one.
			auto unionRel = make_shared<LiteralUnionGraphRelation>();
			unionRel->add(it->relation);
			unionRel->add(relation);
			it->relation = unionRel; 
		}
		else
		{
			VERTEXY_WARN("Variable %d is being referred to be two overlapping literal relations in the same constraint. This will prevent it from being used for graph learning.", lit.variable.raw());
			m_isValid = false;
		}
		return;
	}

	auto itVar = find_if(m_variableRelations.begin(), m_variableRelations.end(), [&](auto&& entry)
	{
		return entry.var == lit.variable;
	});
	if (itVar != m_variableRelations.end())
	{
		VERTEXY_WARN("Variable %d is being referred to by bth variable and literal relations in the same constraint. This will prevent it from being used for graph learning.", lit.variable.raw());
		m_isValid = false;
		return;
	}
	
	m_literalRelations.push_back({lit, relation});	
}

bool ConstraintGraphRelationInfo::getVariableRelation(VarID var, IGraphRelationPtr<VarID>& outRelation) const
{
	auto it = find_if(m_variableRelations.begin(), m_variableRelations.end(), [&](auto&& entry)
	{
		return entry.var == var;
	});
	if (it == m_variableRelations.end())
	{
		return false;
	}
	outRelation = it->relation;
	return true;
}

bool ConstraintGraphRelationInfo::getLiteralRelation(const Literal& lit, IGraphRelationPtr<Literal>& outRelation) const
{
	// We only have a relation if we can match ALL values in the literal
	Literal remaining = lit;
	outRelation = nullptr;

	shared_ptr<LiteralUnionGraphRelation> unionRel = nullptr;

	bool valid = false;
	for (auto& entry : m_literalRelations)
	{
		if (entry.lit.variable == lit.variable && entry.lit.values.isSubsetOf(remaining.values))
		{
			if (outRelation == nullptr)
			{
				vxy_assert(unionRel == nullptr);
				outRelation = entry.relation;
			}
			else
			{
				if (unionRel == nullptr)
				{
					unionRel = make_shared<LiteralUnionGraphRelation>();
					unionRel->add(outRelation);

					outRelation = unionRel;
				}
				unionRel->add(entry.relation);
			}

			remaining.values.exclude(entry.lit.values);
			if (remaining.values.isZero())
			{
				valid = true;
				break;
			}
		}
	}

	if (!valid)
	{
		outRelation = nullptr;
	}
	return valid;
}
