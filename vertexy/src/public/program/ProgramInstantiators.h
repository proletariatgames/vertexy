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
    FunctionInstantiator(FunctionTerm& term, const ProgramCompiler::AtomDomain& domain);

    virtual void first() override;
    virtual void match() override;
    virtual bool hitEnd() const override;

protected:
    FunctionTerm& m_term;
    const ProgramCompiler::AtomDomain& m_domain;
    int m_index;
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