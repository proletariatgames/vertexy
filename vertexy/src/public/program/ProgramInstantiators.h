// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once

#include "ConstraintTypes.h"
#include "program/ProgramCompiler.h"

namespace Vertexy
{

// Instantiator for function terms
class FunctionInstantiator : public Instantiator
{
public:
    FunctionInstantiator(FunctionTerm& term, const ProgramCompiler::AtomDomain& domain, const ITopologyPtr& topology);

    virtual void first(AbstractOverrideMap& overrideMap, ProgramSymbol& boundVertex) override;
    virtual void match(AbstractOverrideMap& overrideMap, ProgramSymbol& boundVertex) override;
    virtual bool hitEnd() const override;

protected:
    void moveNextDomainAtom();
    bool matches(const ProgramSymbol& symbol, AbstractOverrideMap& overrideMap, ProgramSymbol& boundVertex);

    FunctionTerm& m_term;
    const ProgramCompiler::AtomDomain& m_domain;
    const ITopologyPtr& m_topology;
    int m_index = 0;
    int m_subIndex = 0;
    bool m_forceConcrete = false;
    mutable bool m_hitEnd = false;
    hash_set<ProgramSymbol, call_hash> m_visited;
};

class ExternalFunctionInstantiator : public Instantiator
{
public:
    ExternalFunctionInstantiator(FunctionTerm& term);
    virtual void first(AbstractOverrideMap& overrideMap, ProgramSymbol& boundVertex) override;
    virtual void match(AbstractOverrideMap& overrideMap, ProgramSymbol& boundVertex) override;
    virtual bool hitEnd() const override;

protected:
    FunctionTerm& m_term;
    bool m_needsAbstractRelation = false;
    mutable bool m_hitEnd;
};

// Instantiator for <Variable> = <Term> terms
class EqualityInstantiator : public Instantiator
{
public:
    EqualityInstantiator(BinaryOpTerm& term, const ProgramCompiler& compiler);

    virtual void first(AbstractOverrideMap& overrideMap, ProgramSymbol& boundVertex) override;
    virtual void match(AbstractOverrideMap& overrideMap, ProgramSymbol& boundVertex) override;
    virtual bool hitEnd() const override;

protected:
    BinaryOpTerm& m_term;
    const ProgramCompiler& m_compiler;
    mutable bool m_hitEnd;
};

// Instantiator for <Variable> <op> <Term> terms (op = lt/lte/gt/gte/neq)
class RelationInstantiator : public Instantiator
{
public:
    RelationInstantiator(BinaryOpTerm& term, const ProgramCompiler& compiler);

    virtual void first(AbstractOverrideMap& overrideMap, ProgramSymbol& boundVertex) override;
    virtual void match(AbstractOverrideMap& overrideMap, ProgramSymbol& boundVertex) override;
    virtual bool hitEnd() const override;

protected:
    static bool isRelationOp(EBinaryOperatorType op);

    BinaryOpTerm& m_term;
    const ProgramCompiler& m_compiler;
    mutable bool m_hitEnd;
};

// Instantiator for constant values. Only matches on truthiness.
class ConstInstantiator : public Instantiator
{
public:
    ConstInstantiator(bool matched) : m_matched(matched) {}

    virtual void first(AbstractOverrideMap& overrideMap, ProgramSymbol& boundVertex) override
    {
        m_hitEnd = !m_matched;        
    }
    virtual void match(AbstractOverrideMap& overrideMap, ProgramSymbol& boundVertex) override {}
    virtual bool hitEnd() const override
    {
        bool hadHit = m_hitEnd;
        m_hitEnd = true;
        return hadHit;
    }
    
protected:
    bool m_matched;
    mutable bool m_hitEnd = false;
};

}