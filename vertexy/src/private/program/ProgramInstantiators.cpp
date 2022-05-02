// Copyright Proletariat, Inc. All Rights Reserved.
#include "program/ProgramInstantiators.h"

using namespace Vertexy;

FunctionInstantiator::FunctionInstantiator(FunctionTerm& term, const ProgramCompiler::AtomDomain& domain): m_term(term)
    , m_domain(domain)
    , m_index(0)
    , m_hitEnd(false)
{
}

void FunctionInstantiator::first()
{
    m_hitEnd = false;
    m_index = 0;
    match();
}

void FunctionInstantiator::match()
{
    if (m_hitEnd)
    {
        return;
    }

    if (m_term.negated)
    {
        // all variables should be fully bound at this point.
        ProgramSymbol matched = m_term.eval();
        if (matched.isInvalid())
        {
            m_hitEnd = true;
            return;
        }

        vxy_assert(matched.getFormula()->uid == m_term.functionUID);
        auto found = m_domain.map.find(matched);
        bool hasFact = found != m_domain.map.end() && m_domain.list[found->second].isFact;
        if (hasFact && !m_domain.list[found->second].symbol.isNegated())
        {
            m_hitEnd = true;
        }
        else
        {
            m_term.assignedAtom = CompilerAtom{matched, hasFact};
        }
    }
    else
    {
        for (; m_index < m_domain.list.size(); ++m_index)
        {
            const CompilerAtom& atom = m_domain.list[m_index];
            if (atom.symbol.isNegated() && atom.isFact)
            {
                continue;
            }

            if (m_term.match(atom.symbol, atom.isFact))
            {
                ++m_index;
                return;
            }
        }
        m_hitEnd = true;
    }
}

bool FunctionInstantiator::hitEnd() const
{
    bool hadHit = m_hitEnd;
    if (m_term.negated)
    {
        m_hitEnd = true;
    }
    return hadHit;
}

EqualityInstantiator::EqualityInstantiator(BinaryOpTerm& term, const ProgramCompiler& compiler): m_term(term)
    , m_compiler(compiler)
    , m_hitEnd(false)
{
    vxy_assert(term.op == EBinaryOperatorType::Equality);
}

void EqualityInstantiator::first()
{
    m_hitEnd = false;
    match();
}

void EqualityInstantiator::match()
{
    if (m_hitEnd)
    {
        return;
    }

    // all variables in right hand side should be fully bound now
    ProgramSymbol rhsSym = m_term.rhs->eval();
    // TODO: passing isFact=false seems fine here?
    if (!rhsSym.isValid() || !m_term.lhs->match(rhsSym, false))
    {
        m_hitEnd = true;
    }
}

bool EqualityInstantiator::hitEnd() const
{
    bool hadHit = m_hitEnd;
    m_hitEnd = true;
    return hadHit;
}

RelationInstantiator::RelationInstantiator(BinaryOpTerm& term, const ProgramCompiler& compiler): m_term(term)
    , m_compiler(compiler)
    , m_hitEnd(false)
{
    vxy_assert(isRelationOp(m_term.op));
}

void RelationInstantiator::first()
{
    m_hitEnd = false;
    match();
}

void RelationInstantiator::match()
{
    if (m_hitEnd)
    {
        return;
    }

    // variables in non-assignment binary ops should be fully bound now
    ProgramSymbol sym = m_term.eval();
    // BinOpTerm::eval() will return 0 to indicate false.
    if (sym.isInvalid() || sym.getInt() == 0)
    {
        m_hitEnd = true;
    }
}

bool RelationInstantiator::hitEnd() const
{
    bool hadHit = m_hitEnd;
    m_hitEnd = true;
    return hadHit;
}

bool RelationInstantiator::isRelationOp(EBinaryOperatorType op)
{
    switch (op)
    {
    // handled by EqualityInstantiator
    // case EBinaryOperatorType::Equality:
    case EBinaryOperatorType::Inequality:
    case EBinaryOperatorType::LessThan:
    case EBinaryOperatorType::LessThanEq:
    case EBinaryOperatorType::GreaterThan:
    case EBinaryOperatorType::GreaterThanEq:
        return true;
    default:
        return false;
    }
}

