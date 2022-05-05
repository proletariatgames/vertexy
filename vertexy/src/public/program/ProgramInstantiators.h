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

    virtual void first() override;
    virtual void match() override;
    virtual bool hitEnd() const override;

protected:
    void checkForTermAbstracts();
    void moveNextDomainAtom();

    FunctionTerm& m_term;
    const ProgramCompiler::AtomDomain& m_domain;
    const ITopologyPtr& m_topology;
    int m_index = 0;
    int m_subIndex = 0;
    int m_abstractCheckState = -1;
    mutable bool m_hitEnd = false;
};

class ExternalFunctionInstantiator : public Instantiator
{
public:
    ExternalFunctionInstantiator(FunctionTerm& term);
    virtual void first() override;
    virtual void match() override;
    virtual bool hitEnd() const override;

protected:
    FunctionTerm& m_term;
    mutable bool m_hitEnd;
};

// Instantiator for <Variable> = <Term> terms
class EqualityInstantiator : public Instantiator
{
public:
    EqualityInstantiator(BinaryOpTerm& term, const ProgramCompiler& compiler);

    virtual void first() override;
    virtual void match() override;
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

    virtual void first() override;
    virtual void match() override;
    virtual bool hitEnd() const override;

protected:
    static bool isRelationOp(EBinaryOperatorType op);

    BinaryOpTerm& m_term;
    const ProgramCompiler& m_compiler;
    mutable bool m_hitEnd;
};

}