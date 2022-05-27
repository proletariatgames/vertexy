// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once

#include "ConstraintTypes.h"
#include "SignedClause.h"
#include <EASTL/variant.h>

namespace Vertexy
{

template <typename T>
class IGraphRelation;

template<typename T>
using IGraphRelationPtr = shared_ptr<const IGraphRelation<T>>;

template <typename T>
class TTopologyVertexData;

class ITopology;

/** Stores information about the graph relationships between variables in a constraint */
class ConstraintGraphRelationInfo
{
public:
	struct VariableRelation
	{
		VarID var;
		IGraphRelationPtr<VarID> relation;
	};

	struct LiteralRelation
	{
		Literal lit;
		IGraphRelationPtr<Literal> relation;
	};

	ConstraintGraphRelationInfo();
	ConstraintGraphRelationInfo(const shared_ptr<ITopology>& graph, int sourceVertex, const IGraphRelationPtr<bool>& filter=nullptr);

	void invalidate();
	void reset(const shared_ptr<ITopology>& graph, int sourceVertex);
	void reserve(int numVariableRels, int numLiteralRels)
	{
		m_variableRelations.reserve(numVariableRels);
		m_literalRelations.reserve(numLiteralRels);
	}

	bool isValid() const { return m_isValid; }
	const shared_ptr<ITopology>& getGraph() const { return m_graph; }
	int getSourceGraphVertex() const { return m_sourceGraphVertex; }

	void addVariableRelation(VarID var, const IGraphRelationPtr<VarID>& relation);
	void addLiteralRelation(const Literal& lit, const IGraphRelationPtr<Literal>& relation);

	bool getVariableRelation(VarID var, IGraphRelationPtr<VarID>& outRelation) const;
	bool getLiteralRelation(const Literal& lit, IGraphRelationPtr<Literal>& outRelation) const;

	const vector<VariableRelation>& getVariableRelations() const { return m_variableRelations; }
	const vector<LiteralRelation>& getLiteralRelations() const { return m_literalRelations; }

	const IGraphRelationPtr<bool>& getFilter() const { return m_filter; }
	
protected:
	// The filter for this relation: which vertices it can apply to.
	IGraphRelationPtr<bool> m_filter;
	
	// Graph this constraint is associated with.
	shared_ptr<ITopology> m_graph;
	// The vertex within the graph this constraint was instantiated for.
	int m_sourceGraphVertex;
	
	vector<VariableRelation> m_variableRelations;
	vector<LiteralRelation> m_literalRelations;

	// Whether this is valid. Currently we treat the constraint as non-graph if there are multiple relations
	// to the same variable within the same constraint.
	bool m_isValid = true;
};

} // namespace Vertexy