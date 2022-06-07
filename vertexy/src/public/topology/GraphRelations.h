// Copyright Proletariat, Inc. All Rights Reserved.
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
	using VertexID = ITopology::VertexID;

	IGraphRelation()
	{
	}

	virtual ~IGraphRelation()
	{
	}

	template <typename U>
	shared_ptr<const IGraphRelation<typename U::RelationType>> map(const shared_ptr<U>& relation) const;
	template<typename U>
	shared_ptr<const IGraphRelation<T>> filter(U&& filter) const;

	virtual bool equals(const IGraphRelation<T>& rhs) const
	{
		return this == &rhs;
	}

	bool operator==(const IGraphRelation<T>& rhs) const
	{
		return equals(rhs);
	}

	virtual size_t hash() const = 0;

	virtual bool getRelation(VertexID sourceVertex, T& out) const = 0;
	virtual wstring toString() const { return TEXT("Custom"); }
};

template<typename T>
using IGraphRelationPtr = shared_ptr<const IGraphRelation<T>>;

using GraphVertexRelationPtr = IGraphRelationPtr<ITopology::VertexID>;

// Basic graph relation that simply returns the vertex itself.
class IdentityGraphRelation : public IGraphRelation<ITopology::VertexID>
{
private:
	IdentityGraphRelation() {} // should not instantiate; use get() instead.

public:
	static shared_ptr<IdentityGraphRelation> get()
	{
		static shared_ptr<IdentityGraphRelation> inst;
		if (inst == nullptr)
		{
			inst = shared_ptr<IdentityGraphRelation>(new IdentityGraphRelation());
		}
		return inst;
	}

	virtual bool getRelation(VertexID sourceVertex, VertexID& out) const override
	{
		out = sourceVertex;
		return true;
	}
	virtual bool equals(const IGraphRelation<VertexID>& rhs) const override
	{
		return dynamic_cast<const IdentityGraphRelation*>(&rhs) != nullptr;
	}
	virtual wstring toString() const override { return TEXT("I"); }
	virtual size_t hash() const override
	{
		return 0; // TODO: better hash value?
	}
};

// Basic graph relation that always returns the same value
template<typename T>
class ConstantGraphRelation : public IGraphRelation<T>
{
public:
	using VertexID = typename IGraphRelation<T>::VertexID;

	ConstantGraphRelation(const T& val)
		: m_val(val)
	{
	}

	ConstantGraphRelation(T&& val) noexcept
		: m_val(move(val))
	{
	}

	virtual bool getRelation(VertexID sourceVertex, T& out) const override
	{
		out = m_val;
		return true;
	}

	virtual bool equals(const IGraphRelation<T>& rhs) const override
	{
		if (auto rrhs = dynamic_cast<const ConstantGraphRelation*>(&rhs))
		{
			return rrhs->m_val == m_val;
		}
		return false;
	}

	virtual size_t hash() const override
	{
		return eastl::hash<T>()(m_val);
	}

	virtual wstring toString() const override { return eastl::to_wstring(m_val); }

	const T& getConstant() const { return m_val; }
	
protected:
	T m_val;
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

	virtual bool equals(const IGraphRelation<T>& rhs) const override
	{
		if (this == &rhs)
		{
			return true;
		}
		if (auto typedRHS = dynamic_cast<const TMappingGraphRelation<T>*>(&rhs))
		{
			return typedRHS->m_relationFirst->equals(*m_relationFirst.get()) && typedRHS->m_relationSecond->equals(*m_relationSecond.get());
		}
		else
		{
			// Defer to other side to inspect mapping to determine equivalency
			return rhs.equals(*this);
		}
	}

	virtual wstring toString() const override
	{
		return {wstring::CtorSprintf(), TEXT("%s -> %s"), m_relationFirst->toString().c_str(), m_relationSecond->toString().c_str()};
	}

	virtual size_t hash() const override
	{
		return combineHashes(m_relationFirst->hash(), m_relationSecond->hash());
	}

	const shared_ptr<const IGraphRelation<int>>& getFirstRelation() const { return m_relationFirst; }
	const shared_ptr<const IGraphRelation<T>>& getSecondRelation() const { return m_relationSecond; }

protected:
	shared_ptr<const IGraphRelation<int>> m_relationFirst;
	shared_ptr<const IGraphRelation<T>> m_relationSecond;
};

template<typename T, typename U>
class TFilterGraphRelation : public IGraphRelation<T>
{
public:
	TFilterGraphRelation(U&& filter, const shared_ptr<const IGraphRelation<T>>& inner)
		: m_filter(filter)
		, m_inner(inner)
	{		
	}

	virtual bool getRelation(int vertex, T& out) const override
	{
		if (!m_filter(vertex))
		{
			return false;
		}
		return m_inner->getRelation(vertex, out);
	}

	virtual bool equals(const IGraphRelation<T>& rhs) const override
	{
		return (&rhs == this);
	}

	virtual size_t hash() const override
	{
		return m_inner->hash();
	}

	virtual wstring toString() const override
	{
		wstring out;
		out.sprintf(TEXT("Filter(%s)"), m_inner->toString().c_str());
		return out;
	}
	
protected:
	U m_filter;
	shared_ptr<const IGraphRelation<T>> m_inner;
};

// Given a vertex in a graph, return the corresponding value in a TTopologyVertexData object.
template <typename T>
class TVertexToDataGraphRelation : public IGraphRelation<T>
{
public:
	explicit TVertexToDataGraphRelation(const ITopologyPtr& topo, const shared_ptr<TTopologyVertexData<T>>& data)
		: m_topo(topo)
		, m_data(data)
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

	virtual bool equals(const IGraphRelation<T>& rhs) const override
	{
		if (this == &rhs)
		{
			return true;
		}
		auto typedRHS = dynamic_cast<const TVertexToDataGraphRelation<T>*>(&rhs);
		return typedRHS != nullptr && typedRHS->m_topo == m_topo && typedRHS->m_data == m_data;
	}

	virtual wstring toString() const override
	{
		return m_data->getName();
	}

	virtual size_t hash() const override
	{
		return eastl::hash<TTopologyVertexData<T>*>()(m_data.get());
	}

	const shared_ptr<TTopologyVertexData<T>>& getData() const { return m_data; }
	const ITopologyPtr& getTopo() const { return m_topo; }

protected:
	ITopologyPtr m_topo;
	const shared_ptr<TTopologyVertexData<T>> m_data;
};

template<typename T>
class TArrayAccessGraphRelation : public IGraphRelation<T>
{
public:
	explicit TArrayAccessGraphRelation(const shared_ptr<const IGraphRelation<vector<T>>>& arrayRel, int index) : arrayRel(arrayRel), index(index) {}

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

	virtual bool equals(const IGraphRelation<T>& rhs) const override
	{
		if (this == &rhs)
		{
			return true;
		}
		auto typedRHS = dynamic_cast<const TArrayAccessGraphRelation<T>*>(&rhs);
		return typedRHS && typedRHS->arrayRel->equals(*arrayRel.get()) && typedRHS->index == index;
	}

	virtual wstring toString() const override
	{
		return {wstring::CtorSprintf(), TEXT("%s[%d]"), arrayRel->toString().c_str(), index};
	}

	virtual size_t hash() const override
	{
		return combineHashes(eastl::hash<int>()(index), arrayRel->hash());
	}

protected:
	shared_ptr<const IGraphRelation<vector<T>>> arrayRel;
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

	shared_ptr<TManyToOneGraphRelation> clone() const
	{
		auto cloned = make_shared<TManyToOneGraphRelation>();
		cloned->m_relations = m_relations;
		return cloned;
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

	void add(const shared_ptr<const IGraphRelation<T>>& rel, bool checkExists=false)
	{
		auto checkEqual= [&](auto&& lhs)
		{
			return lhs->equals(*rel);
		};

		if (!checkExists || !containsPredicate(m_relations.begin(), m_relations.end(), checkEqual))
		{
			m_relations.push_back(rel);
		}
	}

	virtual bool equals(const IGraphRelation<T>& rhs) const override
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
				return outer->equals(*inner.get());
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

	virtual size_t hash() const override
	{
		size_t out = 0;
		for (auto& rel : m_relations)
		{
			out |= rel->hash();
		}
		return out;
	}

	// Given two relations, creates a TManyToOneRelation containing both of them.
	static shared_ptr<const IGraphRelation<T>> combine(const shared_ptr<const IGraphRelation<T>>& first, const shared_ptr<const IGraphRelation<T>>& second)
	{
		if (first == nullptr)
		{
			return second;
		}
		else if (second == nullptr)
		{
			return first;
		}
		
		if (first->equals(*second))
		{
			return first;
		}

		auto out = make_shared<TManyToOneGraphRelation>();
		
		if (auto firstM2O = dynamic_cast<const TManyToOneGraphRelation*>(first.get()))
		{
			for (auto& rel : firstM2O->getRelations())
			{
				out->add(rel, true);
			}
		}
		else
		{
			out->add(first, true);
		}

		if (auto secondM2O = dynamic_cast<const TManyToOneGraphRelation*>(second.get()))
		{
			for (auto& rel : secondM2O->getRelations())
			{
				out->add(rel, true);
			}
		}
		else
		{
			out->add(second, true);
		}
		
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
	virtual size_t hash() const override;
protected:
	vector<shared_ptr<const IGraphRelation<Literal>>> m_relations;
	wstring m_operator;
};

/** Literal transform relation that combines all potential values */
class LiteralUnionGraphRelation : public LiteralTransformGraphRelation
{
public:
	LiteralUnionGraphRelation();
	virtual bool equals(const IGraphRelation<Literal>& rhs) const override;
	virtual bool combine(ValueSet& dest, const ValueSet& src) const override;
};

/** Literal transform relation that intersects all potential values */
class LiteralIntersectionGraphRelation : public LiteralTransformGraphRelation
{
public:
	LiteralIntersectionGraphRelation();

	virtual bool equals(const IGraphRelation<Literal>& rhs) const override;
	virtual bool combine(ValueSet& dest, const ValueSet& src) const override;
};

/** Transforms a FSignedClause relation into an FLiteral relation. */
class ClauseToLiteralGraphRelation : public IGraphRelation<Literal>
{
public:
	ClauseToLiteralGraphRelation(const ConstraintSolver& solver, const shared_ptr<const IGraphRelation<SignedClause>>& clauseRel);
	virtual bool getRelation(int sourceVertex, Literal& out) const override;
	virtual bool equals(const IGraphRelation<Literal>& rhs) const override;
	virtual wstring toString() const override;
	virtual size_t hash() const override;
protected:
	const ConstraintSolver& m_solver;
	shared_ptr<const IGraphRelation<SignedClause>> m_clauseRel;
};

/** Given a vertex and a topology offset, returns a new vertex. Primarily intended for IGraphRelation::map(). */
class TopologyLinkIndexGraphRelation : public IGraphRelation<int>
{
public:
	TopologyLinkIndexGraphRelation(const ITopologyPtr& topo, const TopologyLink& link);

	virtual bool getRelation(int sourceVertex, int& out) const override;
	virtual bool equals(const IGraphRelation<int>& rhs) const override;
	virtual wstring toString() const override;

	const TopologyLink& getLink() const { return m_link; }
	const ITopologyPtr& getTopo() const { return m_topo; }

	virtual size_t hash() const override;

protected:
	ITopologyPtr m_topo;
	TopologyLink m_link;
};

/** Given a vertex, returns data for the vertex at the specified topology offset */
template <typename T>
class TTopologyLinkGraphRelation : public IGraphRelation<T>
{
public:
	TTopologyLinkGraphRelation(const ITopologyPtr& topo, const shared_ptr<TTopologyVertexData<T>>& data, const TopologyLink& link)
		: m_topo(topo)
		, m_data(data)
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

	virtual bool equals(const IGraphRelation<T>& rhs) const override
	{
		if (this == &rhs)
		{
			return true;
		}
		auto typedRHS = dynamic_cast<const TTopologyLinkGraphRelation<T>*>(&rhs);
		if (typedRHS != nullptr && m_topo == typedRHS->m_topo && m_data == typedRHS->m_data &&
			m_link.isEquivalent(typedRHS->m_link, *m_topo.get()))
		{
			return true;
		}

		if (auto mapping = dynamic_cast<const TMappingGraphRelation<T>*>(&rhs))
		{
			auto innerFirst = dynamic_cast<const TopologyLinkIndexGraphRelation*>(mapping->getFirstRelation().get());
			auto innerSecond = dynamic_cast<const TVertexToDataGraphRelation<T>*>(mapping->getSecondRelation().get());
			if (innerFirst && innerSecond)
			{
				return innerSecond->getData() == m_data && m_topo == innerSecond->getTopo() &&
					innerFirst->getLink().isEquivalent(m_link, *m_topo.get());
			}
		}

		return false;
	}

	const TopologyLink& getLink() const { return m_link; }
	const shared_ptr<TTopologyVertexData<T>>& getData() const { return m_data; }
	const ITopologyPtr& getTopo() const { return m_topo; }

	virtual wstring toString() const override
	{
		return {wstring::CtorSprintf(), TEXT("%s %s"), m_link.toString(m_data->getSource()).c_str(), m_data->getName().c_str()};
	}

	virtual size_t hash() const override
	{
		return m_link.hash();
	}

protected:
	ITopologyPtr m_topo;
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

	virtual bool equals(const IGraphRelation<int>& rhs) const override
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

	virtual size_t hash() const override
	{
		return 0;
	}

protected:
	shared_ptr<EdgeTopology> m_edgeTopology;
};

/** Given a vertex in a graph and an edge index, returns the corresponding vertex at the other side of the edge. **/
template<typename TOPO_TYPE, bool INCOMING_EDGE=false>
class TVertexEdgeToVertexGraphRelation : public IGraphRelation<ITopology::VertexID>
{
public:
	TVertexEdgeToVertexGraphRelation(const shared_ptr<TOPO_TYPE>& topo, int edgeIndex)
		: m_topo(topo)
		, m_edgeIndex(edgeIndex)
	{
	}

	virtual bool getRelation(VertexID sourceVertex, VertexID& out) const override
	{
		if (!INCOMING_EDGE)
		{
			if (!m_topo->getOutgoingDestination(sourceVertex, m_edgeIndex, out))
			{
				return false;
			}
		}
		else
		{
			if (!m_topo->getIncomingSource(sourceVertex, m_edgeIndex, out))
			{
				return false;
			}
		}
		return true;
	}

	virtual bool equals(const IGraphRelation<VertexID>& rhs) const override
	{
		if (this == &rhs)
		{
			return true;
		}
		auto typedRHS = dynamic_cast<const TVertexEdgeToVertexGraphRelation*>(&rhs);
		return typedRHS != nullptr && typedRHS->m_topo == m_topo && typedRHS->m_edgeIndex == m_edgeIndex;
	}

	virtual wstring toString() const override
	{
		return INCOMING_EDGE
			? wstring{wstring::CtorSprintf(), TEXT("IncomingEdge(%s)"), m_topo->edgeIndexToString(m_edgeIndex).c_str()}
			: wstring{wstring::CtorSprintf(), TEXT("OutgoingEdge(%s)"), m_topo->edgeIndexToString(m_edgeIndex).c_str()};
	}

	virtual size_t hash() const override
	{
		return eastl::hash<int>()(m_edgeIndex);
	}

protected:
	shared_ptr<TOPO_TYPE> m_topo;
	int m_edgeIndex;
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

	virtual bool equals(const IGraphRelation<int>& rhs) const override
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
			? wstring{wstring::CtorSprintf(), TEXT("IncomingEdgeInEdgeGraph(%s)"), m_vertexTopo->edgeIndexToString(m_edgeIndex).c_str()}
			: wstring{wstring::CtorSprintf(), TEXT("OutgoingEdgeInEdgeGraph(%s)"), m_vertexTopo->edgeIndexToString(m_edgeIndex).c_str()};
	}

	virtual size_t hash() const override
	{
		return eastl::hash<int>()(m_edgeIndex);
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
	virtual bool equals(const IGraphRelation<Literal>& rhs) const override;
	virtual size_t hash() const override;

	const shared_ptr<const IGraphRelation<Literal>>& getInner() const { return m_inner; }

protected:
	shared_ptr<const IGraphRelation<Literal>> m_inner;
};


class NegateGraphRelation : public IGraphRelation<int>
{
public:
    NegateGraphRelation(const IGraphRelationPtr<int>& child);

    virtual bool equals(const IGraphRelation<int>& rhs) const override;
    virtual bool getRelation(int sourceVertex, int& out) const override;
    virtual size_t hash() const override;
	
    const IGraphRelationPtr<int>& getInner() const { return m_child; }

protected:
    IGraphRelationPtr<int> m_child;
};

class BinOpGraphRelation : public IGraphRelation<int>
{
public:
    BinOpGraphRelation(const IGraphRelationPtr<int>& lhs, const IGraphRelationPtr<int>& rhs, EBinaryOperatorType op);

    virtual bool equals(const IGraphRelation<int>& rhs) const override;
    virtual bool getRelation(int sourceVertex, int& out) const override;
    virtual size_t hash() const override;
    virtual wstring toString() const override;

protected:
    IGraphRelationPtr<int> m_lhs;
    IGraphRelationPtr<int> m_rhs;
    EBinaryOperatorType m_op;
};

template <>
template <typename U>
shared_ptr<const IGraphRelation<typename U::RelationType>> IGraphRelation<int>::map(const shared_ptr<U>& relation) const
{
	if constexpr (is_same_v<typename U::RelationType, int>)
	{
		if (dynamic_cast<const IdentityGraphRelation*>(relation.get()) != nullptr)
		{
			return shared_from_this();
		}
	}
	else if (dynamic_cast<const IdentityGraphRelation*>(this) != nullptr)
	{
		return relation;
	}
	
	return make_shared<TMappingGraphRelation<typename U::RelationType>>(shared_from_this(), const_shared_pointer_cast<U, add_const<U>::type>(relation));
}

template<typename T>
template<typename U>
shared_ptr<const IGraphRelation<T>> IGraphRelation<T>::filter(U&& filter) const
{
	return make_shared<TFilterGraphRelation<T,U>>(forward<U>(filter), shared_from_this());	
}

} // namespace Vertexy