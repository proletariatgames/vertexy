// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once

#include "ConstraintTypes.h"
#include "ProgramTypes.h"

// for non-eastl std::function
#include <functional>

namespace Vertexy
{

template<int ARITY> class Formula;
template<int ARITY> class FormulaResult;

// note: we use std::function here, because eastl::function doesn't provide C++17 deduction guides, which makes
// it so we can't pass in lambdas directly.
template<typename R, typename... ARGS>
using ProgramDefinitionFunctor = std::function<R(ARGS...)>;

template<typename R, typename... ARGS> class ProgramDefinition;

// Returns a function type that returns R and takes N ARG arguments
template<size_t N, typename R, typename ARG, typename... ARGS>
struct ArgumentRepeater { using type = typename ArgumentRepeater<N-1,R,ARG,ARG,ARGS...>::type; };
template<typename R, typename ARG, typename... ARGS>
struct ArgumentRepeater<0, R, ARG, ARGS...> { using type = R(ARGS...); };

// Synthesizes the FormulaResult::bind() callback function's signature.
template<size_t SIZE>
struct FormulaBinder
{
    using type = std::function<typename ArgumentRepeater<SIZE, SignedClause, const ProgramSymbol&>::type>;
};

// Abstract base to call a function passed into FormulaResult::bind with a vector of arguments.
class BindCaller
{
public:
    virtual ~BindCaller() {}
    virtual void call(const vector<ProgramSymbol>& syms) = 0;
};

// Implementation of BindCaller for a given arity
template<size_t ARITY>
class TBindCaller : public BindCaller
{
public:
    TBindCaller(typename FormulaBinder<ARITY>::type&& bindFun)
        : m_bindFun(eastl::move(bindFun))
    {
    }

    virtual void call(const vector<ProgramSymbol>& syms) override
    {
        vxy_assert_msg(syms.size() == ARITY, "wrong number of symbols");
        std::apply(m_bindFun, zip(syms));
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
            return zipper<I+1>(std::tuple_cat(eastl::move(t), std::tuple(syms[I])), syms);
        }
    }

    typename FormulaBinder<ARITY>::type m_bindFun;
};

// Base class for instantiated programs (once they have been given their arguments)
class ProgramInstance
{
public:
    virtual ~ProgramInstance() {}

    void addRule(URuleStatement&& rule)
    {
        m_ruleStatements.push_back(move(rule));
    }

    void addBinder(int formulaUID, unique_ptr<BindCaller>&& binder)
    {
        vxy_assert(m_binders.find(formulaUID) == m_binders.end());
        m_binders[formulaUID] = move(binder);
    }

    const vector<URuleStatement>& getRuleStatements() const { return m_ruleStatements; }

protected:
    hash_map<int, unique_ptr<BindCaller>> m_binders;
    vector<URuleStatement> m_ruleStatements;
};

}