// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once
#include "ProgramTypes.h"
#include "ProgramSymbol.h"

namespace Vertexy
{

// Structure for providing arguments to IExternalFormulaProvider::match(), where
// some arguments may already be bound to values, and other arguments are expected to be bound.
class ExternalFormulaMatchArg
{
    enum class EArgType
    {
        Bound,
        Unbound
    };
    
    explicit ExternalFormulaMatchArg(const shared_ptr<ProgramSymbol>& output)
        : m_argType(EArgType::Unbound)
        , m_inner(output)
    {
    }

    explicit ExternalFormulaMatchArg(const ProgramSymbol& input)
        : m_argType(EArgType::Bound)
        , m_inner(make_shared<ProgramSymbol>(input))
    {
    }

public:
    static ExternalFormulaMatchArg makeUnbound(const shared_ptr<ProgramSymbol>& output)
    {
        return ExternalFormulaMatchArg(output);
    }
    static ExternalFormulaMatchArg makeBound(const ProgramSymbol& input)
    {
        return ExternalFormulaMatchArg(input);
    }

    bool isBound() const { return m_argType != EArgType::Unbound; }
    const ProgramSymbol& get() const
    {
        return *m_inner;
    }

    ProgramSymbol* getWriteable() const
    {
        vxy_assert(!isBound());
        return m_inner.get();
    }

protected:
    EArgType m_argType;
    shared_ptr<ProgramSymbol> m_inner;
};

// Interface to provide built-in formulas/atoms to the ProgramCompiler.
class IExternalFormulaProvider
{
public:
    virtual ~IExternalFormulaProvider() {}

    virtual size_t hash() const = 0;

    // evaluate whether this formula is true with these concrete arguments
    virtual bool eval(const vector<ProgramSymbol>& args) const = 0;
    // Whether this formula can instantiate variables in the specified argument slot
    virtual bool canInstantiate(int argIndex) const = 0;

    // Reset to the beginning of the list of potential matches.
    virtual void startMatching(vector<ExternalFormulaMatchArg>&& args) = 0;
    // Bind to the next set of arguments for this provider.
    virtual bool matchNext(bool& isFact) = 0;
};

using IExternalFormulaProviderPtr = shared_ptr<IExternalFormulaProvider>;

}