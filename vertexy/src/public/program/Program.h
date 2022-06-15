// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once

#include "ConstraintTypes.h"
#include "ProgramTypes.h"

// for non-eastl std::function
#include <functional>

namespace Vertexy
{

template<int ARITY, typename FORMULA_DOMAIN> class Formula;
template<int ARITY, typename FORMULA_DOMAIN> class FormulaResult;

// note: we use std::function here, because eastl::function doesn't provide C++17 deduction guides, which makes
// it so we can't pass in lambdas directly.
template<typename R, typename... ARGS>
using ProgramDefinitionFunctor = std::function<R(ARGS...)>;

template<typename R, typename... ARGS> class ProgramDefinition;

namespace detail
{
    // Returns a function type that returns R and takes N ARG arguments
    template<size_t N, typename R, typename ARG, typename... ARGS>
    struct ArgumentRepeater { using type = typename ArgumentRepeater<N-1,R,ARG,ARG,ARGS...>::type; };
    template<typename R, typename ARG, typename... ARGS>
    struct ArgumentRepeater<0, R, ARG, ARGS...> { using type = R(ARGS...); };
}

// Synthesizes the FormulaResult::bind() callback function's signature.
template<typename RETVAL, size_t SIZE>
struct FormulaBinder
{
    using type = std::function<typename detail::ArgumentRepeater<SIZE, RETVAL, const ProgramSymbol&>::type>;
};

// Abstract base to call a function passed into FormulaResult::bind with a vector of arguments.
class BindCaller
{
public:
    explicit BindCaller(int formulaDomainSize, const vector<int>& domainMapping)
        : m_formulaDomainSize(formulaDomainSize)
        , m_domainMapping(domainMapping)
    {        
    }
    virtual ~BindCaller() {}
    virtual VarID call(const IVariableDomainProvider& provider, const vector<ProgramSymbol>& syms) = 0;

    const vector<int>& getDomainMapping() const { return m_domainMapping; }

protected:
    int m_formulaDomainSize;
    vector<int> m_domainMapping;
};

// Wraps a binder function that returns a VarID.
template<size_t ARITY>
class TBindVarCaller : public BindCaller
{
public:
    TBindVarCaller(int formulaDomainSize, typename FormulaBinder<VarID, ARITY>::type&& bindFun, const vector<int>& domainMapping)
        : BindCaller(formulaDomainSize, domainMapping)
        , m_bindFun(eastl::move(bindFun))
    {
    }

    virtual VarID call(const IVariableDomainProvider& provider, const vector<ProgramSymbol>& syms) override
    {
        vxy_assert_msg(syms.size() == ARITY, "wrong number of symbols");
        return std::apply(m_bindFun, zip(syms));
    }

    // zip up ARITY elements from the vector into a tuple
    static auto zip(const vector<ProgramSymbol>& syms)
    {
        return zipper<0>(std::tuple<>(), syms);
    }

    template<size_t I, typename... Ts>
    static auto zipper(std::tuple<Ts...>&& t, const vector<ProgramSymbol>& syms)
    {
        if constexpr (I == ARITY)
        {
            return t;
        }
        else
        {
            return zipper<I+1>(std::tuple_cat(eastl::move(t), std::make_tuple(syms[I])), syms);
        }
    }

protected:
    typename FormulaBinder<VarID, ARITY>::type m_bindFun;
};

// Base class for instantiated programs (once they have been given their arguments)
class ProgramInstance
{
public:
    using BinderMap = hash_map<FormulaUID, unique_ptr<BindCaller>>;

    explicit ProgramInstance(const ITopologyPtr& topology=nullptr);
    virtual ~ProgramInstance();

    void addRule(URuleStatement&& rule);
    void addBinder(FormulaUID formulaUID, unique_ptr<BindCaller>&& binder);

    const vector<URuleStatement>& getRuleStatements() const { return m_ruleStatements; }
    const BinderMap& getBinders() const { return m_binders; }
    BinderMap& getBinders() { return m_binders; }
    const ITopologyPtr& getTopology() const { return m_topology; }

protected:
    void checkRule(const RuleStatement* stmt) const;
    
    ITopologyPtr m_topology;
    BinderMap m_binders;
    vector<URuleStatement> m_ruleStatements;
};

template<typename ReturnType>
class RProgramInstance : public ProgramInstance
{
public:
    explicit RProgramInstance(const ITopologyPtr& topology = nullptr)
        : ProgramInstance(topology)
    {
    }

    void setResult(ReturnType&& result)
    {
        m_result = move(result);
    }

    const ReturnType& getResult() const { return m_result; }
    
protected:
    ReturnType m_result;
};

// Specialization for void return type
template<>
class RProgramInstance<void> : public ProgramInstance
{
public:
    explicit RProgramInstance(const ITopologyPtr& topology = nullptr)
        : ProgramInstance(topology)
    {
    }
};

using ProgramInstancePtr = shared_ptr<ProgramInstance>;

template<typename ReturnType>
using RProgramInstancePtr = shared_ptr<RProgramInstance<ReturnType>>;

}