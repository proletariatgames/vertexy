// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once

#include "ConstraintTypes.h"
#include "Terms.h"

// for non-eastl std::function
#include <functional>

#define VXY_PARAMETER(name) ProgramParameter name(L#name)
#define VXY_FORMULA(name, arity) Formula<arity> name(L#name)

namespace Vertexy
{

class ProgramBodyTerms;
class ProgramBodyTerm;
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
    using type = std::function<typename ArgumentRepeater<SIZE, void, const ProgramSymbol&>::type>;
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
class ProgramInstanceBase
{
public:
    virtual ~ProgramInstanceBase() {}

    void addRule(URuleStatement&& rule)
    {
        m_ruleStatements.push_back(move(rule));
    }

    void addBinder(int formulaUID, unique_ptr<BindCaller>&& binder)
    {
        vxy_assert(m_binders.find(formulaUID) == m_binders.end());
        m_binders[formulaUID] = move(binder);
    }

protected:
    hash_map<int, unique_ptr<BindCaller>> m_binders;
    vector<URuleStatement> m_ruleStatements;
};

// ProgramInstance returned from applying a ProgramDefinition (via the () operator)
template<typename RESULT>
class ProgramInstance : public ProgramInstanceBase
{
public:
    ProgramInstance() {}

    // The typed result returned from the definition
    RESULT result;
};

// Static functions for defining rule programs
class Program
{
protected:
    static ProgramInstanceBase* s_currentInstance;
    static int s_nextFormulaUID;
    static int s_nextParameterUID;
public:
    Program() = delete;
    ~Program() = delete;

    static ProgramInstanceBase* getCurrentInstance() { return s_currentInstance; }

    template<typename R, typename... ARGS>
    static unique_ptr<ProgramInstance<R>> runDefinition(const ProgramDefinitionFunctor<R, ARGS...>& fn, ARGS&&... args)
    {
        auto inst = make_unique<ProgramInstance<R>>();
        vxy_assert_msg(s_currentInstance == nullptr, "Cannot define two programs simultaneously!");
        s_currentInstance = inst.get();

        inst->result = fn(forward<ARGS>(args)...);

        s_currentInstance = nullptr;
        return inst;
    }

    template<typename R, typename... ARGS>
    static ProgramDefinition<R, ARGS...> defineFunctor(const ProgramDefinitionFunctor<R, ARGS...>& definition)
    {
        return ProgramDefinition<R, ARGS...>(definition);
    }

    template<typename T>
    static auto define(T&& definition)
    {
        std::function func {definition};
        return defineFunctor(func);
    }

    static void disallow(ProgramBodyTerm&& body);
    static void disallow(ProgramBodyTerms&& body);

    static Formula<1> range(int min, int max);

    static int allocateFormulaUID() { return s_nextFormulaUID++; }
    static int allocateParameterUID() { return s_nextParameterUID++; }
};

// ProgramDefinition is return from Program::define(). Contains the "code"
// for the program, which can be turned into a ProgramInstance by specifying the program's arguments.
template<typename R, typename... ARGS>
class ProgramDefinition
{
public:
    ProgramDefinition(const ProgramDefinitionFunctor<R, ARGS...>& definition)
        : m_definition(definition)
    {
    }

    // "parse" the definition, returning the ProgramInstance.
    inline unique_ptr<ProgramInstance<R>> apply(ARGS&&... args)
    {
        return Program::runDefinition<R, ARGS...>(m_definition, forward<ARGS>(args)...);
    }

    unique_ptr<ProgramInstance<R>> operator()(ARGS&&... args)
    {
        return apply(eastl::move(args)...);
    }

protected:
    ProgramDefinitionFunctor<R, ARGS...> m_definition;
};

}