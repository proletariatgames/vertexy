// Copyright Proletariat, Inc. All Rights Reserved.
#include "topology/GraphRelations.h"
#include "ConstraintSolver.h"

using namespace Vertexy;

ClauseToLiteralGraphRelation::ClauseToLiteralGraphRelation(const ConstraintSolver& solver, const shared_ptr<const IGraphRelation<SignedClause>>& clauseRel)
	: m_solver(solver)
	, m_clauseRel(clauseRel)
{
}

bool ClauseToLiteralGraphRelation::getRelation(int sourceVertex, Literal& out) const
{
	SignedClause clause;
	if (!m_clauseRel->getRelation(sourceVertex, clause))
	{
		return false;
	}

	out = Literal(clause.variable, clause.translateToDomain(m_solver.getDomain(clause.variable)));
	return true;
}

bool ClauseToLiteralGraphRelation::equals(const IGraphRelation<Literal>& rhs) const
{
	if (this == &rhs)
	{
		return true;
	}
	auto typedRHS = dynamic_cast<const ClauseToLiteralGraphRelation*>(&rhs);
	return typedRHS != nullptr && (&typedRHS->m_solver) == (&m_solver) && m_clauseRel->equals(*typedRHS->m_clauseRel.get());
}

wstring ClauseToLiteralGraphRelation::toString() const
{
	return {wstring::CtorSprintf(), TEXT("ToLiteral(%s)"), m_clauseRel->toString().c_str()};
}

size_t ClauseToLiteralGraphRelation::hash() const
{
	return m_clauseRel->hash();
}

TopologyLinkIndexGraphRelation::TopologyLinkIndexGraphRelation(const ITopologyPtr& topo, const TopologyLink& link): m_topo(topo)
	, m_link(link)
{
}

bool TopologyLinkIndexGraphRelation::getRelation(int sourceVertex, int& out) const
{
	int destVertex;
	if (!m_link.resolve(m_topo, sourceVertex, destVertex))
	{
		return false;
	}

	out = destVertex;
	return true;
}

bool TopologyLinkIndexGraphRelation::equals(const IGraphRelation<int>& rhs) const
{
	if (this == &rhs)
	{
		return true;
	}
	auto typedRHS = dynamic_cast<const TopologyLinkIndexGraphRelation*>(&rhs);
	return typedRHS != nullptr && m_topo == typedRHS->m_topo &&
		m_link.isEquivalent(typedRHS->m_link, *m_topo.get());
}

wstring TopologyLinkIndexGraphRelation::toString() const
{
	return m_link.toString(m_topo);
}

size_t TopologyLinkIndexGraphRelation::hash() const
{
	return m_link.hash();
}

LiteralTransformGraphRelation::LiteralTransformGraphRelation(const wchar_t* operatorName)
	: m_operator(operatorName)
{
}

void LiteralTransformGraphRelation::add(const shared_ptr<const IGraphRelation<Literal>>& rel)
{
	if (!contains(m_relations.begin(), m_relations.end(), rel))
	{
		m_relations.push_back(rel);
	}
}

size_t LiteralTransformGraphRelation::hash() const
{
	size_t hash = 0;
	for (auto& rel : m_relations)
	{
		hash |= rel->hash();
	}
	return hash;
}

bool LiteralTransformGraphRelation::getRelation(int sourceVertex, Literal& out) const
{
	if (m_relations.empty())
	{
		return false;
	}

	Literal val;
	if (!m_relations[0]->getRelation(sourceVertex, val))
	{
		return false;
	}

	Literal otherVal;
	for (int i = 1; i < m_relations.size(); ++i)
	{
		if (!m_relations[i]->getRelation(sourceVertex, otherVal) ||
			val.variable != otherVal.variable)
		{
			return false;
		}
		if (!combine(val.values, otherVal.values))
		{
			return false;
		}
	}
	out = val;
	return true;
}

wstring LiteralTransformGraphRelation::toString() const
{
	wstring out = TEXT("(");
	for (int i = 0; i < m_relations.size(); ++i)
	{
		out.append_sprintf(TEXT("%s%s"), m_relations[i]->toString().c_str(), i == m_relations.size() - 1 ? TEXT("") : m_operator.c_str());
	}
	out.append(TEXT(")"));
	return out;
}

bool InvertLiteralGraphRelation::getRelation(int sourceVertex, Literal& out) const
{
	if (!m_inner->getRelation(sourceVertex, out))
	{
		return false;
	}
	out.values.invert();
	return true;
}

InvertLiteralGraphRelation::InvertLiteralGraphRelation(const shared_ptr<const IGraphRelation<Literal>>& inner)
	: m_inner(inner)
{
}

bool InvertLiteralGraphRelation::equals(const IGraphRelation<Literal>& rhs) const
{
	if (this == &rhs)
	{
		return true;
	}
	auto typedRHS = dynamic_cast<const InvertLiteralGraphRelation*>(&rhs);
	return typedRHS != nullptr && m_inner->equals(*typedRHS->m_inner.get());
}

size_t InvertLiteralGraphRelation::hash() const
{
	return m_inner->hash();
}

NegateGraphRelation::NegateGraphRelation(const IGraphRelationPtr<int>& child): m_child(child)
{
}

bool NegateGraphRelation::equals(const IGraphRelation<int>& rhs) const
{
	if (auto typed = dynamic_cast<const NegateGraphRelation*>(&rhs))
	{
		return typed->m_child->equals(*m_child.get());
	}
	return false;
}

bool NegateGraphRelation::getRelation(int sourceVertex, int& out) const
{
	if (m_child->getRelation(sourceVertex, out))
	{
		out *= -1;
		return true;
	}
	return false;
}

size_t NegateGraphRelation::hash() const
{
	return m_child->hash();
}

BinOpGraphRelation::BinOpGraphRelation(const IGraphRelationPtr<int>& lhs, const IGraphRelationPtr<int>& rhs, EBinaryOperatorType op)
	: m_lhs(lhs)
	, m_rhs(rhs)
	, m_op(op)
{
}

bool BinOpGraphRelation::equals(const IGraphRelation<int>& rhs) const
{
	if (auto typed = dynamic_cast<const BinOpGraphRelation*>(&rhs))
	{
		return
			typed->m_op == m_op &&
			typed->m_lhs->equals(*m_lhs.get()) &&
			typed->m_rhs->equals(*m_rhs.get());
	}
	return false;
}

bool BinOpGraphRelation::getRelation(int sourceVertex, int& out) const
{
	int left, right;
	if (m_lhs->getRelation(sourceVertex, left) && m_rhs->getRelation(sourceVertex, right))
	{
		switch (m_op)
		{
		case EBinaryOperatorType::Add:
			out = left+right;
			break;
		case EBinaryOperatorType::Subtract:
			out = left-right;
			break;
		case EBinaryOperatorType::Divide:
			out = left/right;
			break;
		case EBinaryOperatorType::Multiply:
			out = left*right;
			break;
		case EBinaryOperatorType::Equality:
			if (left != right) { return false; }
			out = 1;
			break;
		case EBinaryOperatorType::Inequality:
			if (left == right) { return false; }
			out = 1;
			break;
		case EBinaryOperatorType::LessThan:
			if (left >= right) { return false; }
			out = 1;
			break;
		case EBinaryOperatorType::LessThanEq:
			if (left > right) { return false; }
			out = 1;
			break;
		case EBinaryOperatorType::GreaterThan:
			if (left <= right) { return false; }
			out = 1;
			break;
		case EBinaryOperatorType::GreaterThanEq:
			if (left < right) { return false; }
			out = 1;
		default:
			vxy_fail_msg("unexpected binary operator");
		}
		return true;
	}
	return false;
}

size_t BinOpGraphRelation::hash() const
{
	return combineHashes(m_lhs->hash(),
	                     combineHashes(m_rhs->hash(), eastl::hash<EBinaryOperatorType>()(m_op))
	);
}

wstring BinOpGraphRelation::toString() const
{
	wstring sOp;
	switch (m_op)
	{
	case EBinaryOperatorType::Add:				sOp = TEXT("+"); break;
	case EBinaryOperatorType::Subtract:			sOp = TEXT("-"); break;
	case EBinaryOperatorType::Divide:			sOp = TEXT("/"); break;
	case EBinaryOperatorType::Multiply:			sOp = TEXT("*"); break;
	case EBinaryOperatorType::Equality:			sOp = TEXT("=="); break;
	case EBinaryOperatorType::Inequality:		sOp = TEXT("!="); break;
	case EBinaryOperatorType::LessThan:			sOp = TEXT("<"); break;
	case EBinaryOperatorType::LessThanEq:		sOp = TEXT("<="); break;
	case EBinaryOperatorType::GreaterThan:		sOp = TEXT(">"); break;
	case EBinaryOperatorType::GreaterThanEq:	sOp = TEXT(">="); break;
	default:
		vxy_fail_msg("unexpected binary operator");
	}

	wstring out = m_lhs->toString();
	out.append_sprintf(TEXT(" %s %s"), sOp.c_str(), m_rhs->toString().c_str());
	return out;
}

wstring InvertLiteralGraphRelation::toString() const
{
	return {wstring::CtorSprintf(), TEXT("InvertLiteral(%s)"), m_inner->toString().c_str()};
}

LiteralUnionGraphRelation::LiteralUnionGraphRelation()
	: LiteralTransformGraphRelation(TEXT(" | "))
{
}

bool LiteralUnionGraphRelation::equals(const IGraphRelation<Literal>& rhs) const
{
	if (this == &rhs)
	{
		return true;
	}
	auto typedRHS = dynamic_cast<const LiteralUnionGraphRelation*>(&rhs);
	if (typedRHS == nullptr)
	{
		return false;
	}
	return !containsPredicate(m_relations.begin(), m_relations.end(), [&](auto&& outer)
	{
		return !containsPredicate(typedRHS->m_relations.begin(), typedRHS->m_relations.end(), [&](auto&& inner)
		{
			return inner->equals(*outer.get());
		});
	});
}

bool LiteralUnionGraphRelation::combine(ValueSet& dest, const ValueSet& src) const
{
	dest.include(src);
	return true;
}

LiteralIntersectionGraphRelation::LiteralIntersectionGraphRelation()
	: LiteralTransformGraphRelation(TEXT(" & "))
{
}

bool LiteralIntersectionGraphRelation::equals(const IGraphRelation<Literal>& rhs) const
{
	if (this == &rhs)
	{
		return true;
	}
	auto typedRHS = dynamic_cast<const LiteralIntersectionGraphRelation*>(&rhs);
	if (typedRHS == nullptr)
	{
		return false;
	}
	return !containsPredicate(m_relations.begin(), m_relations.end(), [&](auto&& outer)
	{
		return !containsPredicate(typedRHS->m_relations.begin(), typedRHS->m_relations.end(), [&](auto&& inner)
		{
			return inner->equals(*outer.get());
		});
	});
}

bool LiteralIntersectionGraphRelation::combine(ValueSet& dest, const ValueSet& src) const
{
	dest.intersect(src);
	return true;
}
