// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once

#include "ConstraintTypes.h"
#include "Terms.h"

// for non-eastl std::function
#include <functional>

namespace  Vertexy
{

template<int ARITY> class Formula;
class ProgramFunctionTerm;
class ProgramHeadTerm;
class ProgramBodyTerm;
class ProgramBodyTerms;
class ProgramInstance;

class ProgramInstance
{
public:
    void addRule(URuleStatement&& rule)
    {
        m_ruleStatements.push_back(move(rule));
    }

protected:
    vector<URuleStatement> m_ruleStatements;
};

template<typename... ARGS> class ProgramDefinition;

// note: we use std::function here, because eastl::function doesn't provide C++17 deduction guides
template<typename... ARGS>
using ProgramDefinitionFunctor = std::function<void(ARGS...)>;

class Program
{
protected:
    static ProgramInstance* s_currentInstance;
    static int s_nextFormulaUID;
    static int s_nextParameterUID;
public:
    static ProgramInstance* getCurrentInstance() { return s_currentInstance; }

    template<typename... ARGS>
    static unique_ptr<ProgramInstance> runDefinition(const ProgramDefinitionFunctor<ARGS...>& fn, ARGS&&... args)
    {
        auto inst = make_unique<ProgramInstance>();
        vxy_assert_msg(s_currentInstance == nullptr, "Cannot define two programs simultaneously!");
        s_currentInstance = inst.get();

        fn(forward<ARGS>(args)...);

        s_currentInstance = nullptr;
        return inst;
    }

    template<typename... ARGS>
    static ProgramDefinition<ARGS...> defineFunctor(const ProgramDefinitionFunctor<ARGS...>& definition)
    {
        return ProgramDefinition<ARGS...>(definition);
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
    explicit ProgramFunctionTerm(int uid, vector<ProgramBodyTerm>&& args) : uid(uid), args(move(args)) {}
    int uid;
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
        term = make_unique<FunctionTerm>(f.uid, move(argTerms));
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
        term = make_unique<FunctionTerm>(f.uid, move(argTerms));
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
public:
    Formula()
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
        return ProgramFunctionTerm(m_uid, move(fargs));
    }

private:
    template<typename T, typename... REM>
    void foldArgs(vector<ProgramBodyTerm>& outArgs, T&& arg, REM&&... rem)
    {
        outArgs.push_back(move(arg));
        foldArgs(outArgs, rem...);
    }
    void foldArgs(vector<ProgramBodyTerm>& outArgs) {}

    int m_uid;
};


template<typename... ARGS>
class ProgramDefinition
{
public:
    ProgramDefinition(const ProgramDefinitionFunctor<ARGS...>& definition)
        : m_definition(definition)
    {
    }

    unique_ptr<ProgramInstance> operator()(ARGS&&... args) const
    {
        return Program::runDefinition<ARGS...>(m_definition, forward<ARGS>(args)...);
    }

protected:
    ProgramDefinitionFunctor<ARGS...> m_definition;
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
    return Formula<1>();
}


}