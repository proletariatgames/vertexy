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

wstring InvertLiteralGraphRelation::toString() const
{
	return {wstring::CtorSprintf(), TEXT("InvertLiteral(%s)"), m_inner->toString().c_str()};
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
