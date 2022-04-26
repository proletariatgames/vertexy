﻿// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once

#include "ConstraintTypes.h"
#include "SignedClause.h"
#include "topology/DigraphEdgeTopology.h"
#include "topology/TopologyVertexData.h"
#include "topology/TopologyLink.h"

namespace Vertexy
{
// Interface for a mapping between a vertices in a graph and values.
template <typename T>
class IGraphRelation : public enable_shared_from_this<IGraphRelation<T>>
{
public:
	using RelationType = T;

	IGraphRelation()
	{
	}

	virtual ~IGraphRelation()
	{
	}

	template <typename U>
	shared_ptr<IGraphRelation<typename U::RelationType>> map(const shared_ptr<U>& relation) const;

	virtual bool equals(const shared_ptr<const ITopology>& topology, const IGraphRelation<T>& rhs) const
	{
		return this == &rhs;
	}

	virtual bool getRelation(int sourceVertex, T& out) const = 0;
	virtual wstring toString() const { return TEXT("Custom"); }
};


/** Combines a Vertex->Int relation and an Int->Value relation into a Vertex->Value relation */
template <typename T>
class TMappingGraphRelation : public IGraphRelation<T>
{
public:
	TMappingGraphRelation(const shared_ptr<const IGraphRelation<int>>& relationFirst, const shared_ptr<const IGraphRelation<T>>& relationSecond)
		: m_relationFirst(relationFirst)
		, m_relationSecond(relationSecond)
	{
	}

	virtual bool getRelation(int vertex, T& out) const override
	{
		int mapped;
		if (!m_relationFirst->getRelation(vertex, mapped))
		{
			return false;
		}
		if (!m_relationSecond->getRelation(mapped, out))
		{
			return false;
		}
		return true;
	}

	virtual bool equals(const shared_ptr<const ITopology>& topology, const IGraphRelation<T>& rhs) const override
	{
		if (this == &rhs)
		{
			return true;
		}
		if (auto typedRHS = dynamic_cast<const TMappingGraphRelation<T>*>(&rhs))
		{
			return typedRHS->m_relationFirst->equals(topology, *m_relationFirst.get()) && typedRHS->m_relationSecond->equals(topology, *m_relationSecond.get());
		}
		else
		{
			// Defer to other side to inspect mapping to determine equivalency
			return rhs.equals(topology, *this);
		}
	}

	virtual wstring toString() const override
	{
		return {wstring::CtorSprintf(), TEXT("%s -> %s"), m_relationFirst->toString().c_str(), m_relationSecond->toString().c_str()};
	}

	const shared_ptr<const IGraphRelation<int>>& getFirstRelation() const { return m_relationFirst; }
	const shared_ptr<const IGraphRelation<T>>& getSecondRelation() const { return m_relationSecond; }

protected:
	shared_ptr<const IGraphRelation<int>> m_relationFirst;
	shared_ptr<const IGraphRelation<T>> m_relationSecond;
};

// Given a vertex in a graph, return the corresponding value in a TTopologyVertexData object.
template <typename T>
class TVertexToDataGraphRelation : public IGraphRelation<T>
{
public:
	explicit TVertexToDataGraphRelation(const shared_ptr<TTopologyVertexData<T>>& data)
		: m_data(data)
	{
	}

	virtual bool getRelation(int sourceVertex, T& out) const override
	{
		if (!m_data->getSource()->isValidVertex(sourceVertex))
		{
			return false;
		}
		out = m_data->get(sourceVertex);
		return true;
	}

	virtual bool equals(const shared_ptr<const ITopology>& topology, const IGraphRelation<T>& rhs) const override
	{
		if (this == &rhs)
		{
			return true;
		}
		auto typedRHS = dynamic_cast<const TVertexToDataGraphRelation<T>*>(&rhs);
		return typedRHS != nullptr && typedRHS->m_data == m_data;
	}

	virtual wstring toString() const override
	{
		return m_data->getName();
	}

	const shared_ptr<TTopologyVertexData<T>>& getData() const { return m_data; }

protected:
	const shared_ptr<TTopologyVertexData<T>> m_data;
};

template<typename T>
class TArrayAccessGraphRelation : public IGraphRelation<T>
{
public:
	explicit TArrayAccessGraphRelation(const shared_ptr<IGraphRelation<vector<T>>>& arrayRel, int index) : arrayRel(arrayRel), index(index) {}

	virtual bool getRelation(int sourceVertex, T& out) const override
	{
		vector<T> found;
		if (!arrayRel->getRelation(sourceVertex, found))
		{
			return false;
		}

		if (index < 0 || index >= found.size())
		{
			return false;
		}

		out = found[index];
		return true;
	}

	virtual bool equals(const shared_ptr<const ITopology>& topology, const IGraphRelation<T>& rhs) const override
	{
		if (this == &rhs)
		{
			return true;
		}
		auto typedRHS = dynamic_cast<const TArrayAccessGraphRelation<T>*>(&rhs);
		return typedRHS && typedRHS->arrayRel->equals(topology, *arrayRel.get()) && typedRHS->index == index;
	}

	virtual wstring toString() const override
	{
		return {wstring::CtorSprintf(), TEXT("%s[%d]"), arrayRel->toString().c_str(), index};
	}

protected:
	shared_ptr<IGraphRelation<vector<T>>> arrayRel;
	int index;
};

// Given multiple relations, return the relation only if all of them evaluate to the same value.
template <typename T>
class TManyToOneGraphRelation : public IGraphRelation<T>
{
public:
	TManyToOneGraphRelation()
	{
	}

	virtual bool getRelation(int sourceVertex, T& out) const override
	{
		if (m_relations.empty())
		{
			return false;
		}

		T val;
		if (!m_relations[0]->getRelation(sourceVertex, val))
		{
			return false;
		}

		for (int i = 1; i < m_relations.size(); ++i)
		{
			T otherVal;
			if (!m_relations[i]->getRelation(sourceVertex, otherVal) || val != otherVal)
			{
				return false;
			}
		}
		out = val;
		return true;
	}

	void add(const shared_ptr<const IGraphRelation<T>>& rel)
	{
		m_relations.push_back(rel);
	}

	virtual bool equals(const shared_ptr<const ITopology>& topology, const IGraphRelation<T>& rhs) const override
	{
		if (this == &rhs)
		{
			return true;
		}
		auto typedRHS = dynamic_cast<const TManyToOneGraphRelation<T>*>(&rhs);
		if (typedRHS == nullptr)
		{
			return false;
		}

		return !containsPredicate(m_relations.begin(), m_relations.end(), [&](auto&& outer)
		{
			return !containsPredicate(typedRHS->getRelations().begin(), typedRHS->getRelations().end(), [&](auto&& inner)
			{
				return outer->equals(topology, *inner.get());
			});
		});
	}

	virtual wstring toString() const override
	{
		wstring out = TEXT("Resolve(");
		for (int i = 0; i < m_relations.size(); ++i)
		{
			out.append_sprintf(TEXT("%s%s"), m_relations[i]->toString().c_str(), i == m_relations.size() - 1 ? TEXT("") : TEXT(", "));
		}
		out.append(TEXT(")"));
		return out;
	}

	const vector<shared_ptr<const IGraphRelation<T>>>& getRelations() const { return m_relations; }

protected:
	vector<shared_ptr<const IGraphRelation<T>>> m_relations;
};

// Base class for Vertex->FLiteral graph relations, where an array of Vertex->FLiteral relations is provided.
// The subclass defines how these literals are concatenated.
class LiteralTransformGraphRelation : public IGraphRelation<Literal>
{
public:
	virtual bool combine(ValueSet& dest, const ValueSet& src) const = 0;

	LiteralTransformGraphRelation(const wchar_t* operatorName);
	virtual bool getRelation(int sourceVertex, Literal& out) const override;
	virtual wstring toString() const override;

	void add(const shared_ptr<const IGraphRelation<Literal>>& rel);
protected:
	vector<shared_ptr<const IGraphRelation<Literal>>> m_relations;
	wstring m_operator;
};

/** Literal transform relation that combines all potential values */
class LiteralUnionGraphRelation : public LiteralTransformGraphRelation
{
public:
	LiteralUnionGraphRelation()
		: LiteralTransformGraphRelation(TEXT(" | "))
	{
	}

	virtual bool equals(const shared_ptr<const ITopology>& topology, const IGraphRelation<Literal>& rhs) const override;

	virtual bool combine(ValueSet& dest, const ValueSet& src) const override
	{
		dest.include(src);
		return true;
	}
};

/** Literal transform relation that intersects all potential values */
class LiteralIntersectionGraphRelation : public LiteralTransformGraphRelation
{
public:
	LiteralIntersectionGraphRelation()
		: LiteralTransformGraphRelation(TEXT(" & "))
	{
	}

	virtual bool equals(const shared_ptr<const ITopology>& topology, const IGraphRelation<Literal>& rhs) const override;

	virtual bool combine(ValueSet& dest, const ValueSet& src) const override
	{
		dest.intersect(src);
		return true;
	}
};

/** Transforms a FSignedClause relation into an FLiteral relation. */
class ClauseToLiteralGraphRelation : public IGraphRelation<Literal>
{
public:
	ClauseToLiteralGraphRelation(const ConstraintSolver& solver, const shared_ptr<const IGraphRelation<SignedClause>>& clauseRel);
	virtual bool getRelation(int sourceVertex, Literal& out) const override;
	virtual bool equals(const shared_ptr<const ITopology>& topology, const IGraphRelation<Literal>& rhs) const override;
	virtual wstring toString() const override;
protected:
	const ConstraintSolver& m_solver;
	shared_ptr<const IGraphRelation<SignedClause>> m_clauseRel;
};

/** Given a vertex and a topology offset, returns a new vertex. Primarily intended for IGraphRelation::map(). */
class TopologyLinkIndexGraphRelation : public IGraphRelation<int>
{
public:
	TopologyLinkIndexGraphRelation(const shared_ptr<ITopology>& topo, const TopologyLink& link)
		: m_topo(topo)
		, m_link(link)
	{
	}

	virtual bool getRelation(int sourceVertex, int& out) const override
	{
		int destVertex;
		if (!m_link.resolve(m_topo, sourceVertex, destVertex))
		{
			return false;
		}

		out = destVertex;
		return true;
	}

	virtual bool equals(const shared_ptr<const ITopology>& topology, const IGraphRelation<int>& rhs) const override
	{
		if (this == &rhs)
		{
			return true;
		}
		auto typedRHS = dynamic_cast<const TopologyLinkIndexGraphRelation*>(&rhs);
		return typedRHS != nullptr && m_topo == typedRHS->m_topo && m_link.isEquivalent(typedRHS->m_link, *topology.get());
	}

	virtual wstring toString() const override
	{
		return m_link.toString(m_topo);
	}

	const TopologyLink& getLink() const { return m_link; }

protected:
	shared_ptr<ITopology> m_topo;
	TopologyLink m_link;
};

/** Given a vertex, returns data for the vertex at the specified topology offset */
template <typename T>
class TTopologyLinkGraphRelation : public IGraphRelation<T>
{
public:
	TTopologyLinkGraphRelation(const shared_ptr<TTopologyVertexData<T>>& data, const TopologyLink& link)
		: m_data(data)
		, m_link(link)
	{
	}

	virtual bool getRelation(int sourceVertex, T& out) const override
	{
		int destVertex;
		if (!m_link.resolve(m_data->getSource(), sourceVertex, destVertex))
		{
			return false;
		}

		out = m_data->get(destVertex);
		return true;
	}

	virtual bool equals(const shared_ptr<const ITopology>& topology, const IGraphRelation<T>& rhs) const override
	{
		if (this == &rhs)
		{
			return true;
		}
		auto typedRHS = dynamic_cast<const TTopologyLinkGraphRelation<T>*>(&rhs);
		if (typedRHS != nullptr && m_data == typedRHS->m_data && m_link.isEquivalent(typedRHS->m_link, *topology.get()))
		{
			return true;
		}

		if (auto mapping = dynamic_cast<const TMappingGraphRelation<T>*>(&rhs))
		{
			auto innerFirst = dynamic_cast<const TopologyLinkIndexGraphRelation*>(mapping->getFirstRelation().get());
			auto innerSecond = dynamic_cast<const TVertexToDataGraphRelation<T>*>(mapping->getSecondRelation().get());
			if (innerFirst && innerSecond)
			{
				return innerSecond->getData() == m_data && innerFirst->getLink().isEquivalent(m_link, *topology.get());
			}
		}

		return false;
	}

	const TopologyLink& getLink() const { return m_link; }
	const shared_ptr<TTopologyVertexData<T>>& getData() const { return m_data; }

	virtual wstring toString() const override
	{
		return {wstring::CtorSprintf(), TEXT("%s %s"), m_link.toString(m_data->getSource()).c_str(), m_data->getName().c_str()};
	}

protected:
	const shared_ptr<TTopologyVertexData<T>> m_data;
	TopologyLink m_link;
};

/** Given a vertex in an FEdgeTopology, returns either the source or destination vertex in the FEdgeTopology's source graph. */
template <bool INCOMING_EDGE = false>
class TGraphEdgeToVertexIndexRelation : public IGraphRelation<int>
{
public:
	explicit TGraphEdgeToVertexIndexRelation(const shared_ptr<EdgeTopology>& edgeTopology)
		: m_edgeTopology(edgeTopology)
	{
	}

	virtual bool getRelation(int edgeNode, int& out) const override
	{
		int vertexFrom, vertexTo;
		bool bidirectional;
		m_edgeTopology->getSourceEdgeForVertex(edgeNode, vertexFrom, vertexTo, bidirectional);

		if constexpr (INCOMING_EDGE)
		{
			out = vertexFrom;
		}
		else
		{
			out = vertexTo;
		}
		return true;
	}

	virtual bool equals(const shared_ptr<const ITopology>& topology, const IGraphRelation<int>& rhs) const override
	{
		if (this == &rhs)
		{
			return true;
		}
		auto typedRHS = dynamic_cast<const TGraphEdgeToVertexIndexRelation<INCOMING_EDGE>*>(&rhs);
		return typedRHS != nullptr && typedRHS->m_edgeTopology == m_edgeTopology;
	}

	virtual wstring toString() const override
	{
		return INCOMING_EDGE ? TEXT("IncomingVertexForEdge") : TEXT("OutgoingVertexForEdge");
	}


protected:
	shared_ptr<EdgeTopology> m_edgeTopology;
};

/** Given a vertex in a graph, an edge index, and a FEdgeTopology for the graph, and returns the corresponding vertex index in the edge graph */
template <typename TOPO_TYPE, bool INCOMING_EDGE = false>
class TVertexEdgeToEdgeGraphVertexGraphRelation : public IGraphRelation<int>
{
public:
	TVertexEdgeToEdgeGraphVertexGraphRelation(const shared_ptr<TOPO_TYPE>& vertexTopo, const shared_ptr<EdgeTopology>& edgeTopo, int edgeIndex)
		: m_vertexTopo(vertexTopo)
		, m_edgeTopo(edgeTopo)
		, m_edgeIndex(edgeIndex)
	{
	}

	virtual bool getRelation(int vertex, int& out) const override
	{
		int destVertex;
		if constexpr (!INCOMING_EDGE)
		{
			if (!m_vertexTopo->getOutgoingDestination(vertex, m_edgeIndex, destVertex))
			{
				return false;
			}
		}
		else
		{
			if (!m_vertexTopo->getIncomingSource(vertex, m_edgeIndex, destVertex))
			{
				return false;
			}
		}

		int edgeNode = m_edgeTopo->getVertexForSourceEdge(vertex, destVertex);
		vxy_assert(m_edgeTopo->isValidVertex(edgeNode));

		out = edgeNode;
		return true;
	}

	virtual bool equals(const shared_ptr<const ITopology>& topology, const IGraphRelation<int>& rhs) const override
	{
		if (this == &rhs)
		{
			return true;
		}
		auto typedRHS = dynamic_cast<const TVertexEdgeToEdgeGraphVertexGraphRelation*>(&rhs);
		return typedRHS != nullptr && typedRHS->m_vertexTopo == m_vertexTopo && typedRHS->m_edgeTopo == m_edgeTopo && typedRHS->m_edgeIndex == m_edgeIndex;
	}

	virtual wstring toString() const override
	{
		return INCOMING_EDGE
				   ? wstring{wstring::CtorSprintf(), TEXT("IncomingEdge(%s)"), m_vertexTopo->edgeIndexToString(m_edgeIndex).c_str()}
		: wstring{wstring::CtorSprintf(), TEXT("OutgoingEdge(%s)"), m_vertexTopo->edgeIndexToString(m_edgeIndex).c_str()};
	}

protected:
	shared_ptr<TOPO_TYPE> m_vertexTopo;
	shared_ptr<EdgeTopology> m_edgeTopo;
	int m_edgeIndex;
};

/** Maps a Vertex->FLiteral relation to the inverse of the literal */
class InvertLiteralGraphRelation : public IGraphRelation<Literal>
{
public:
	InvertLiteralGraphRelation(const shared_ptr<const IGraphRelation<Literal>>& inner);
	virtual bool getRelation(int sourceVertex, Literal& out) const override;
	virtual wstring toString() const override;
	virtual bool equals(const shared_ptr<const ITopology>& topology, const IGraphRelation<Literal>& rhs) const override;

	const shared_ptr<const IGraphRelation<Literal>>& getInner() const { return m_inner; }

protected:
	shared_ptr<const IGraphRelation<Literal>> m_inner;
};

template <>
template <typename U>
shared_ptr<IGraphRelation<typename U::RelationType>> IGraphRelation<int>::map(const shared_ptr<U>& relation) const
{
	return make_shared<TMappingGraphRelation<typename U::RelationType>>(shared_from_this(), const_shared_pointer_cast<U, add_const<U>::type>(relation));
}

} // namespace Vertexy