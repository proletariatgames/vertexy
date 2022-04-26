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

// note: we use std::function here, because eastl::function doesn't provide C++17 deduction guides, which makes
// it so we can't pass in lambdas directly.
template<typename R, typename... ARGS>
using ProgramDefinitionFunctor = std::function<R(ARGS...)>;

// Returns a function type that returns R and takes N ARG arguments
template<size_t N, typename R, typename ARG, typename... ARGS>
struct ArgumentRepeater { using type = typename ArgumentRepeater<N-1,R,ARG,ARG,ARGS...>::type; };
template<typename R, typename ARG, typename... ARGS>
struct ArgumentRepeater<0, R, ARG, ARGS...> { using type = R(ARGS...); };

template<int ARITY> class Formula;
template<int ARITY> class FormulaResult;

class ProgramFunctionTerm;
class ProgramHeadTerm;
class ProgramBodyTerm;
class ProgramBodyTerms;

template<size_t SIZE>
struct FormulaBinder {
    using type =  std::function<typename ArgumentRepeater<SIZE, void, const ProgramSymbol&>::type>;
};

class BindCaller
{
public:
    virtual ~BindCaller() {}
    virtual void call(const vector<ProgramSymbol>& syms) = 0;
};

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
            return zipper<I+1>(std::tuple_cat(t, std::tuple(syms[I])), syms);
        }
    }

    typename FormulaBinder<ARITY>::type m_bindFun;
};

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

template<typename RESULT>
class ProgramInstance : public ProgramInstanceBase
{
public:
    ProgramInstance() {}

    RESULT result;
};

template<typename R, typename... ARGS> class ProgramDefinition;

class Program
{
protected:
    static ProgramInstanceBase* s_currentInstance;
    static int s_nextFormulaUID;
    static int s_nextParameterUID;
public:
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

class ProgramBinaryOpArgument
{
public:
    ProgramBinaryOpArgument(int constant)
    {
        term = make_unique<SymbolTerm>(ProgramSymbol(constant));
    }
    ProgramBinaryOpArgument(const ProgramSymbol& sym)
    {
        term = make_unique<SymbolTerm>(sym);
    }
    ProgramBinaryOpArgument(ProgramParameter param)
    {
        term = make_unique<VariableTerm>(param);
    }

    explicit ProgramBinaryOpArgument(UTerm&& term) : term(move(term))
    {
    }

    UTerm term;
};


// formula with terms applied to each argument
class ProgramFunctionTerm
{
public:
    explicit ProgramFunctionTerm(int uid, const wchar_t* name, vector<ProgramBodyTerm>&& args) : uid(uid), name(name), args(move(args)) {}
    int uid;
    const wchar_t* name;
    vector<ProgramBodyTerm> args;
};

class ProgramBodyTerm
{
protected:
    ProgramBodyTerm();

public:
    ProgramBodyTerm(int constant)
    {
        term = make_unique<SymbolTerm>(ProgramSymbol(constant));
    }
    ProgramBodyTerm(const ProgramSymbol& s)
    {
        term = make_unique<SymbolTerm>(s);
    }
    ProgramBodyTerm(const ProgramParameter& p)
    {
        term = make_unique<VariableTerm>(p);
    }
    ProgramBodyTerm(ProgramFunctionTerm&& f)
    {
        vector<UTerm> argTerms;
        argTerms.reserve(f.args.size());

        for (ProgramBodyTerm& arg : f.args)
        {
            argTerms.push_back(move(arg.term));
        }
        term = make_unique<FunctionTerm>(f.uid, f.name, move(argTerms));
    }
    ProgramBodyTerm(ProgramBinaryOpArgument&& h)
    {
        term = move(h.term);
    }

    explicit ProgramBodyTerm(UTerm&& inTerm)
    {
        term = move(inTerm);
    }

    UTerm term;
};

class ProgramBodyTerms
{
public:
    explicit ProgramBodyTerms(vector<UTerm>&& inTerms) : terms(move(inTerms))
    {
    };
    ProgramBodyTerms(ProgramBodyTerm&& rhs)
    {
        terms.push_back(move(rhs.term));
    }
    void add(UTerm&& child)
    {
        terms.push_back(move(child));
    }

    vector<UTerm> terms;
};


inline ProgramBodyTerms operator&&(ProgramBodyTerm&& lhs, ProgramBodyTerm&& rhs)
{
    vector<UTerm> terms;
    terms.reserve(2);
    terms.push_back(move(lhs.term));
    terms.push_back(move(rhs.term));
    return ProgramBodyTerms(move(terms));
}

inline ProgramBodyTerms operator&&(ProgramBodyTerms&& lhs, ProgramBodyTerm&& rhs)
{
    vector<UTerm> terms = move(lhs.terms);
    terms.push_back(move(rhs.term));
    return ProgramBodyTerms(move(terms));
}

inline ProgramBodyTerm operator~(ProgramBodyTerm&& rhs)
{
    UTerm term = make_unique<UnaryOpTerm>(EUnaryOperatorType::Negate, move(rhs.term));
    return ProgramBodyTerm(move(term));
}

inline ProgramBinaryOpArgument operator<(ProgramBinaryOpArgument&& lhs, ProgramBinaryOpArgument&& rhs)
{
    UTerm term = make_unique<BinaryOpTerm>(EBinaryOperatorType::LessThan, move(lhs.term), move(rhs.term));
    return ProgramBinaryOpArgument(move(term));
}

inline ProgramBinaryOpArgument operator<=(ProgramBinaryOpArgument&& lhs, ProgramBinaryOpArgument&& rhs)
{
    UTerm term = make_unique<BinaryOpTerm>(EBinaryOperatorType::LessThanEq, move(lhs.term), move(rhs.term));
    return ProgramBinaryOpArgument(move(term));
}
inline ProgramBinaryOpArgument operator>(ProgramBinaryOpArgument&& lhs, ProgramBinaryOpArgument&& rhs)
{
    UTerm term = make_unique<BinaryOpTerm>(EBinaryOperatorType::GreaterThan, move(lhs.term), move(rhs.term));
    return ProgramBinaryOpArgument(move(term));
}
inline ProgramBinaryOpArgument operator>=(ProgramBinaryOpArgument&& lhs, ProgramBinaryOpArgument&& rhs)
{
    UTerm term = make_unique<BinaryOpTerm>(EBinaryOperatorType::GreaterThanEq, move(lhs.term), move(rhs.term));
    return ProgramBinaryOpArgument(move(term));
}
inline ProgramBinaryOpArgument operator==(ProgramBinaryOpArgument&& lhs, ProgramBinaryOpArgument&& rhs)
{
    UTerm term = make_unique<BinaryOpTerm>(EBinaryOperatorType::Equality, move(lhs.term), move(rhs.term));
    return ProgramBinaryOpArgument(move(term));
}
inline ProgramBinaryOpArgument operator!=(ProgramBinaryOpArgument&& lhs, ProgramBinaryOpArgument&& rhs)
{
    UTerm term = make_unique<BinaryOpTerm>(EBinaryOperatorType::Inequality, move(lhs.term), move(rhs.term));
    return ProgramBinaryOpArgument(move(term));
}
inline ProgramBinaryOpArgument operator*(ProgramBinaryOpArgument&& lhs, ProgramBinaryOpArgument&& rhs)
{
    UTerm term = make_unique<BinaryOpTerm>(EBinaryOperatorType::Multiply, move(lhs.term), move(rhs.term));
    return ProgramBinaryOpArgument(move(term));
}
inline ProgramBinaryOpArgument operator/(ProgramBinaryOpArgument&& lhs, ProgramBinaryOpArgument&& rhs)
{
    UTerm term = make_unique<BinaryOpTerm>(EBinaryOperatorType::Divide, move(lhs.term), move(rhs.term));
    return ProgramBinaryOpArgument(move(term));
}
inline ProgramBinaryOpArgument operator+(ProgramBinaryOpArgument&& lhs, ProgramBinaryOpArgument&& rhs)
{
    UTerm term = make_unique<BinaryOpTerm>(EBinaryOperatorType::Add, move(lhs.term), move(rhs.term));
    return ProgramBinaryOpArgument(move(term));
}
inline ProgramBinaryOpArgument operator-(ProgramBinaryOpArgument&& lhs, ProgramBinaryOpArgument&& rhs)
{
    UTerm term = make_unique<BinaryOpTerm>(EBinaryOperatorType::Subtract, move(lhs.term), move(rhs.term));
    return ProgramBinaryOpArgument(move(term));
}

class ProgramHeadTerm
{
public:
    ProgramHeadTerm(ProgramFunctionTerm&& f)
    {
        vector<UTerm> argTerms;
        argTerms.reserve(f.args.size());

        for (ProgramBodyTerm& arg : f.args)
        {
            argTerms.push_back(move(arg.term));
        }
        term = make_unique<FunctionTerm>(f.uid, f.name, move(argTerms));
    }

    explicit ProgramHeadTerm(UTerm&& term) : term(move(term))
    {
    }

    UTerm term;
};

class ProgramHeadDisjunctionTerm : public ProgramHeadTerm
{
public:
    explicit ProgramHeadDisjunctionTerm(unique_ptr<DisjunctionTerm>&& term) : ProgramHeadTerm(move(term)) {}
    void add(UTerm&& child)
    {
        static_cast<DisjunctionTerm*>(term.get())->children.push_back(move(child));
    }
};

inline ProgramHeadDisjunctionTerm operator|(ProgramHeadTerm&& lhs, ProgramHeadTerm&& rhs)
{
    vector<UTerm> children;
    children.reserve(2);
    children.push_back(move(lhs.term));
    children.push_back(move(rhs.term));
    return ProgramHeadDisjunctionTerm(make_unique<DisjunctionTerm>(move(children)));
}
inline ProgramHeadDisjunctionTerm& operator|(ProgramHeadDisjunctionTerm& lhs, ProgramHeadTerm&& rhs)
{
    lhs.add(move(rhs.term));
    return lhs;
}

inline void operator<<=(ProgramHeadTerm&& head, ProgramBodyTerms&& body)
{
    vxy_assert_msg(Program::getCurrentInstance() != nullptr, "Cannot specify rules outside of a Program::define block!");
    auto rule = make_unique<RuleStatement>(move(head.term), move(body.terms));
    Program::getCurrentInstance()->addRule(move(rule));
}

inline void operator<<=(ProgramHeadTerm&& head, ProgramBodyTerm&& body)
{
    vector<UTerm> terms;
    terms.push_back(move(body.term));
    operator<<=(forward<ProgramHeadTerm>(head), ProgramBodyTerms(move(terms)));
}

template<int ARITY>
class Formula
{
    friend FormulaResult;

public:
    Formula(const wchar_t* name=nullptr)
        : m_name(name)
    {
        vxy_assert_msg(Program::getCurrentInstance() != nullptr, "Cannot create a Formula outside of a Program::define block!");
        m_uid = Program::allocateFormulaUID();
    }

    template<typename... ARGS>
    ProgramFunctionTerm operator()(ARGS&&... args)
    {
        static_assert(sizeof...(args) == ARITY, "Wrong number of arguments for formula");
        vector<ProgramBodyTerm> fargs;
        fargs.reserve(ARITY);

        foldArgs(fargs, args...);
        return ProgramFunctionTerm(m_uid, m_name, move(fargs));
    }

private:
    template<typename T, typename... REM>
    void foldArgs(vector<ProgramBodyTerm>& outArgs, T&& arg, REM&&... rem)
    {
        outArgs.push_back(move(arg));
        foldArgs(outArgs, rem...);
    }
    void foldArgs(vector<ProgramBodyTerm>& outArgs) {}

    const wchar_t* m_name;
    int m_uid;
};

template<int ARITY>
class FormulaResult
{
public:
    FormulaResult()
    {
        m_instance = nullptr;
        m_formulaUID = -1;
        m_formulaName = nullptr;
    }

    FormulaResult(const Formula<ARITY>& formula)
    {
        vxy_assert_msg(Program::getCurrentInstance() != nullptr, "Cannot construct a FormulaResult outside of a Program::define block!");
        m_instance = Program::getCurrentInstance();
        m_formulaName = formula.m_name;
        m_formulaUID = formula.m_uid;
    }

    void bind(typename FormulaBinder<ARITY>::type&& binder)
    {
        vxy_assert_msg(m_instance && m_formulaUID >= 0, "FormulaResult not bound to a formula");
        m_instance->addBinder(m_formulaUID, eastl::make_unique<TBindCaller<ARITY>>(eastl::move(binder)));
    }

    ProgramInstanceBase* m_instance;
    const wchar_t* m_formulaName;
    int m_formulaUID;
};

template<typename R, typename... ARGS>
class ProgramDefinition
{
public:
    ProgramDefinition(const ProgramDefinitionFunctor<R, ARGS...>& definition)
        : m_definition(definition)
    {
    }

    unique_ptr<ProgramInstance<R>> operator()(ARGS&&... args) const
    {
        return Program::runDefinition<R, ARGS...>(m_definition, forward<ARGS>(args)...);
    }

protected:
    ProgramDefinitionFunctor<R, ARGS...> m_definition;
};

inline void Program::disallow(ProgramBodyTerm&& body)
{
    return disallow(ProgramBodyTerms(forward<ProgramBodyTerm>(body)));
}

inline void Program::disallow(ProgramBodyTerms&& body)
{
    vxy_assert_msg(Program::getCurrentInstance() != nullptr, "Cannot specify rules outside of a Program::define block!");
    vector<UTerm> terms;
    auto rule = make_unique<RuleStatement>(nullptr, move(body.terms));
    s_currentInstance->addRule(move(rule));
}

inline Formula<1> Program::range(int min, int max)
{
    return Formula<1>(L"range");
}


}