// Copyright Proletariat, Inc. All Rights Reserved.
#include "program/ProgramInstantiators.h"

using namespace Vertexy;

FunctionInstantiator::FunctionInstantiator(FunctionTerm& term, const ProgramCompiler::AtomDomain& domain, const ITopologyPtr& topology)
    : m_term(term)
    , m_domain(domain)
    , m_topology(topology)
{
    vxy_assert(m_term.provider == nullptr);
}

void FunctionInstantiator::first(AbstractOverrideMap& overrideMap, ProgramSymbol& boundVertex)
{
    m_hitEnd = false;
    m_index = 0;
    m_subIndex = 0;
    m_abstractCheckState = -1;
    match(overrideMap, boundVertex);
}

void FunctionInstantiator::match(AbstractOverrideMap& overrideMap, ProgramSymbol& boundVertex)
{
    vxy_assert(m_term.provider == nullptr);
    if (m_hitEnd)
    {
        return;
    }

    if (m_term.negated)
    {
        // all variables should be fully bound at this point, because positive literals are always earlier in
        // the dependency list. Therefore, we can eval safely.
        ProgramSymbol matched = m_term.eval(overrideMap, boundVertex);
        if (matched.isInvalid())
        {
            m_hitEnd = true;
            return;
        }

        vxy_assert(matched.getFormula()->uid == m_term.functionUID);
        auto found = m_domain.map.find(matched.negatedFormula());
        bool hasFact = found != m_domain.map.end() && m_domain.list[found->second].isFact;

        // if this has definitely been established as true, we can't match.
        if (hasFact)
        {
            m_hitEnd = true;
        }

        m_term.assignedToFact = hasFact;
    }
    else
    {
        for (; m_index < m_domain.list.size(); moveNextDomainAtom())
        {
            const CompilerAtom& atom = m_domain.list[m_index];
            if (atom.symbol.isNegated() && atom.isFact)
            {
                continue;
            }
            
            // if (atom.symbol.containsAbstract())
            // {
            //     checkForTermAbstracts();
            //     if (m_abstractCheckState == 0)
            //     {
            //         // This term contains no abstracts, so we need to ground the abstract domain atom
            //         for (; m_subIndex < m_topology->getNumVertices(); ++m_subIndex)
            //         {
            //             ProgramSymbol concreteSymbol = atom.symbol.makeConcrete(m_subIndex);
            //             if (!concreteSymbol.isValid())
            //             {
            //                 continue;
            //             }
            //
            //             bool isFact = atom.isFact;
            //             if (m_term.match(concreteSymbol, isFact))
            //             {
            //                 ++m_subIndex;
            //                 return;
            //             }
            //         }
            //
            //         continue;
            //     }
            // }

            bool isFact = atom.isFact;
            if (m_term.match(atom.symbol, overrideMap, boundVertex))
            {
                m_term.assignedToFact = isFact;
                moveNextDomainAtom();
                return;
            }
        }
        m_hitEnd = true;
    }
}

void FunctionInstantiator::moveNextDomainAtom()
{
    ++m_index;
    m_abstractCheckState = -1;
    m_subIndex = 0;
}

void FunctionInstantiator::checkForTermAbstracts()
{
    if (m_abstractCheckState < 0)
    {
        // See if this term (now) contains abstracts
        m_abstractCheckState = 0;
        m_term.visit([&](const Term* term)
        {
            if (dynamic_cast<const VertexTerm*>(term) != nullptr)
            {
               m_abstractCheckState = 1;
               return Term::EVisitResponse::Abort;
            }
            else if (auto varTerm = dynamic_cast<const VariableTerm*>(term))
            {
                if (varTerm->sharedBoundRef != nullptr && varTerm->sharedBoundRef->isAbstract())
                {
                    m_abstractCheckState = 1;
                    return Term::EVisitResponse::Abort;
                }
            }
            return Term::EVisitResponse::Continue;
        });
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

ExternalFunctionInstantiator::ExternalFunctionInstantiator(FunctionTerm& term)
    : m_term(term)
    , m_hitEnd(false)
{
    vxy_assert(m_term.provider != nullptr);
}

void ExternalFunctionInstantiator::first(AbstractOverrideMap& overrideMap, ProgramSymbol& boundVertex)
{
    m_hitEnd = false;

    bool allArgumentsBound = true;
    bool anyAbstractArguments = false;
    
    vector<ExternalFormulaMatchArg> matchArgs;
    matchArgs.reserve(m_term.arguments.size());
    for (auto& arg : m_term.arguments)
    {
        if (auto varArg = dynamic_cast<VariableTerm*>(arg.get()))
        {
            if (varArg->isBinder)
            {
                allArgumentsBound = false;
                matchArgs.push_back(ExternalFormulaMatchArg::makeUnboundOrOverridable(varArg->sharedBoundRef));
            }
            else
            {
                ProgramSymbol boundVarVal = varArg->eval(overrideMap, boundVertex);
                matchArgs.push_back(ExternalFormulaMatchArg::makeBound(boundVarVal));
                if (boundVarVal.isAbstract())
                {
                    anyAbstractArguments = true;
                }
            }
        }
        else if (auto symArg = dynamic_cast<SymbolTerm*>(arg.get()))
        {
            matchArgs.push_back(ExternalFormulaMatchArg::makeBound(symArg->sym));
        }
        else if (auto vertexArg = dynamic_cast<VertexTerm*>(arg.get()))
        {
            matchArgs.push_back(ExternalFormulaMatchArg::makeBound(vertexArg->eval(overrideMap, boundVertex)));
        }
        else
        {
            vxy_fail_msg("Unsupported external formula argument");
            m_hitEnd = true;
            return;
        }
    }

    m_needsAbstractRelation = (allArgumentsBound && anyAbstractArguments);
    if (!m_needsAbstractRelation)
    {
        m_term.provider->startMatching(move(matchArgs));
    }
    match(overrideMap, boundVertex);
}

void ExternalFunctionInstantiator::match(AbstractOverrideMap&, ProgramSymbol&)
{
    vxy_assert(m_term.provider != nullptr);
    if (m_hitEnd)
    {
        return;
    }

    if (m_needsAbstractRelation)
    {
        m_term.assignedToFact = false;
    }
    else
    {
        bool isFact = false;
        m_hitEnd = !m_term.provider->matchNext(isFact);

        if (m_term.negated && !m_hitEnd && isFact)
        {
            m_hitEnd = true;
            return;
        }

        m_term.assignedToFact = isFact;
    }
}

bool ExternalFunctionInstantiator::hitEnd() const
{
    bool hadHit = m_hitEnd;
    if (m_term.negated || m_needsAbstractRelation)
    {
        m_hitEnd = true;
    }
    return hadHit;
}

EqualityInstantiator::EqualityInstantiator(BinaryOpTerm& term, const ProgramCompiler& compiler)
    : m_term(term)
    , m_compiler(compiler)
    , m_hitEnd(false)
{
    vxy_assert(term.op == EBinaryOperatorType::Equality);
}

void EqualityInstantiator::first(AbstractOverrideMap& overrideMap, ProgramSymbol& boundVertex)
{
    m_hitEnd = false;
    match(overrideMap, boundVertex);
}

void EqualityInstantiator::match(AbstractOverrideMap& overrideMap, ProgramSymbol& boundVertex)
{
    if (m_hitEnd)
    {
        return;
    }

    // all variables in right hand side should be fully bound now
    ProgramSymbol rhsSym = m_term.rhs->eval(overrideMap, boundVertex);
    if (rhsSym.isAbstract())
    {
        // If the right hand side is abstract, we may need to create an abstract relation.
        ProgramSymbol sym = m_term.eval(overrideMap, boundVertex);
        if (sym.isInvalid())
        {
            m_hitEnd = true;
        }
    }
    else
    {
        if (!rhsSym.isValid() || !m_term.lhs->match(rhsSym, overrideMap, boundVertex))
        {
            m_hitEnd = true;
        }
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

void RelationInstantiator::first(AbstractOverrideMap& overrideMap, ProgramSymbol& boundVertex)
{
    m_hitEnd = false;
    match(overrideMap, boundVertex);
}

void RelationInstantiator::match(AbstractOverrideMap& overrideMap, ProgramSymbol& boundVertex)
{
    if (m_hitEnd)
    {
        return;
    }

    // variables in non-assignment binary ops should be fully bound now
    ProgramSymbol sym = m_term.eval(overrideMap, boundVertex);
    // BinOpTerm::eval() will return 0 to indicate false.
    if (sym.isInvalid() || (sym.isInteger() && sym.getInt() == 0))
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

