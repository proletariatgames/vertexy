// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once
#include "ConstraintTypes.h"
#include "program/Program.h"

#define VXY_VARIABLE(name) ProgramVariable name(L#name)
#define VXY_FORMULA(name, arity) Formula<arity> name(L#name)

/**
 * Implementation of the mini language for defining rules inside a Program::define() block
 */
namespace Vertexy
{

class ProgramFunctionTerm;
class ProgramHeadTerm;
class ProgramBodyTerm;
class ProgramBodyTerms;

template<int ARITY> class Formula;
template<int ARITY> class FormulaResult;

class ProgramOpArgument
{
public:
    ProgramOpArgument(int constant)
    {
        term = make_unique<SymbolTerm>(ProgramSymbol(constant));
    }
    ProgramOpArgument(const ProgramSymbol& sym)
    {
        term = make_unique<SymbolTerm>(sym);
    }
    ProgramOpArgument(ProgramVariable param)
    {
        term = make_unique<VariableTerm>(param);
    }

    explicit ProgramOpArgument(UTerm&& term) : term(move(term))
    {
    }

    UTerm term;
};

class ProgramHeadChoiceTerm
{
public:
    explicit ProgramHeadChoiceTerm(unique_ptr<ChoiceTerm>&& term)
        : term(move(term))
        , bound(false)
    {
    }

    ~ProgramHeadChoiceTerm();

    UTerm term;
    bool bound;
};

class ProgramHeadDisjunctionTerm
{
public:
    explicit ProgramHeadDisjunctionTerm(unique_ptr<DisjunctionTerm>&& term)
        : term(move(term))
        , bound(false)
    {
    }

    ~ProgramHeadDisjunctionTerm();

    void add(UTerm&& child)
    {
        static_cast<DisjunctionTerm*>(term.get())->children.push_back(move(child));
    }

    UTerm term;
    bool bound;
};

// formula with terms applied to each argument. Can appear in either body or head.
class ProgramFunctionTerm
{
public:
    explicit ProgramFunctionTerm(FormulaUID uid, const wchar_t* name, vector<ProgramBodyTerm>&& args)
        : uid(uid)
        , name(name)
        , args(move(args))
        , bound(false)
    {
    }

    ~ProgramFunctionTerm();

    ProgramHeadChoiceTerm choice();

    FormulaUID uid;
    const wchar_t* name;
    vector<ProgramBodyTerm> args;
    bool bound;

protected:
    UTerm createTerm();
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
    ProgramBodyTerm(const ProgramVariable& p)
    {
        term = make_unique<VariableTerm>(p);
    }
    ProgramBodyTerm(ProgramFunctionTerm&& f)
    {
        f.bound = true;

        vector<UTerm> argTerms;
        argTerms.reserve(f.args.size());

        for (ProgramBodyTerm& arg : f.args)
        {
            argTerms.push_back(move(arg.term));
        }
        term = make_unique<FunctionTerm>(f.uid, f.name, move(argTerms), false);
    }
    ProgramBodyTerm(ProgramOpArgument&& h)
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

class ProgramHeadTerm
{
public:
    ProgramHeadTerm(ProgramFunctionTerm&& f)
    {
        bound = false;
        f.bound = true;

        vector<UTerm> argTerms;
        argTerms.reserve(f.args.size());

        for (ProgramBodyTerm& arg : f.args)
        {
            argTerms.push_back(move(arg.term));
        }
        term = make_unique<FunctionTerm>(f.uid, f.name, move(argTerms), false);
    }

    explicit ProgramHeadTerm(UTerm&& term) : term(move(term))
    {
    }

    ~ProgramHeadTerm();

    UTerm term;
    bool bound = false;
};

// TProgramInstance returned from applying a ProgramDefinition (via the () operator)
template<typename RESULT>
class TProgramInstance : public ProgramInstance
{
public:
    TProgramInstance() {}

    // The typed result returned from the definition
    RESULT result;
};

template<>
class TProgramInstance<void> : public ProgramInstance
{
public:
    TProgramInstance() {}
};

// Static functions for defining rule programs
class Program
{
protected:
    static ProgramInstance* s_currentInstance;
    static int s_nextFormulaUID;
    static int s_nextVarUID;
public:
    Program() = delete;
    ~Program() = delete;

    static ProgramInstance* getCurrentInstance() { return s_currentInstance; }

    template<typename R, typename... ARGS>
    static unique_ptr<TProgramInstance<R>> runDefinition(const ProgramDefinitionFunctor<R, ARGS...>& fn, ARGS&&... args)
    {
        auto inst = make_unique<TProgramInstance<R>>();
        vxy_assert_msg(s_currentInstance == nullptr, "Cannot define two programs simultaneously!");
        s_currentInstance = inst.get();

        if constexpr (is_same_v<R, void>)
        {
            fn(forward<ARGS>(args)...);
        }
        else
        {
            inst->result = fn(forward<ARGS>(args)...);
        }

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

    static Formula<1> range(ProgramSymbol min, ProgramSymbol max);

    static FormulaUID allocateFormulaUID()
    {
        FormulaUID out = static_cast<FormulaUID>(s_nextFormulaUID);
        s_nextFormulaUID++;
        return out;
    }
    static VariableUID allocateVariableUID()
    {
        VariableUID out = static_cast<VariableUID>(s_nextVarUID);
        s_nextVarUID++;
        return out;
    }
};

// ProgramDefinition is return from Program::define(). Contains the "code"
// for the program, which can be turned into a TProgramInstance by specifying the program's arguments.
template<typename R, typename... ARGS>
class ProgramDefinition
{
public:
    ProgramDefinition(const ProgramDefinitionFunctor<R, ARGS...>& definition)
        : m_definition(definition)
    {
    }

    // "parse" the definition, returning the TProgramInstance.
    inline unique_ptr<TProgramInstance<R>> apply(ARGS&&... args)
    {
        return Program::runDefinition<R, ARGS...>(m_definition, forward<ARGS>(args)...);
    }

    unique_ptr<TProgramInstance<R>> operator()(ARGS&&... args)
    {
        return apply(eastl::move(args)...);
    }

protected:
    ProgramDefinitionFunctor<R, ARGS...> m_definition;
};

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
    FormulaUID m_uid;
};

template<int ARITY>
class FormulaResult
{
public:
    FormulaResult()
    {
        m_instance = nullptr;
        m_formulaUID = FormulaUID(-1);
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

    ProgramInstance* m_instance;
    const wchar_t* m_formulaName;
    FormulaUID m_formulaUID;
};

/*******************************************************
 *
 * Operator Overloads
 *
 */

inline ProgramHeadChoiceTerm ProgramFunctionTerm::choice()
{
    bound = true;
    auto choiceTerm = make_unique<ChoiceTerm>(createTerm());
    return ProgramHeadChoiceTerm(move(choiceTerm));
}

inline UTerm ProgramFunctionTerm::createTerm()
{
    vector<UTerm> argTerms;
    argTerms.reserve(args.size());

    for (ProgramBodyTerm& arg : args)
    {
        argTerms.push_back(move(arg.term));
    }
    return make_unique<FunctionTerm>(uid, name, move(argTerms), false);
}

inline ProgramFunctionTerm::~ProgramFunctionTerm()
{
    if (!bound)
    {
        // fact
        vxy_assert_msg(Program::getCurrentInstance() != nullptr, "Cannot specify rules outside of a Program::define block!");
        auto rule = make_unique<RuleStatement>(createTerm());
        Program::getCurrentInstance()->addRule(move(rule));
    }
}

inline ProgramHeadChoiceTerm::~ProgramHeadChoiceTerm()
{
    if (!bound)
    {
        // fact
        vxy_assert_msg(Program::getCurrentInstance() != nullptr, "Cannot specify rules outside of a Program::define block!");
        auto rule = make_unique<RuleStatement>(move(term));
        Program::getCurrentInstance()->addRule(move(rule));
    }
}

inline ProgramHeadDisjunctionTerm::~ProgramHeadDisjunctionTerm()
{
    if (!bound)
    {
        // fact
        vxy_assert_msg(Program::getCurrentInstance() != nullptr, "Cannot specify rules outside of a Program::define block!");
        auto rule = make_unique<RuleStatement>(move(term));
        Program::getCurrentInstance()->addRule(move(rule));
    }
}

inline ProgramHeadTerm::~ProgramHeadTerm()
{
    if (!bound)
    {
        // fact
        vxy_assert_msg(Program::getCurrentInstance() != nullptr, "Cannot specify rules outside of a Program::define block!");
        auto rule = make_unique<RuleStatement>(move(term));
        Program::getCurrentInstance()->addRule(move(rule));
    }
}

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

inline ProgramBodyTerm operator~(ProgramFunctionTerm&& rhs)
{
    rhs.bound = true;

    vector<UTerm> argTerms;
    argTerms.reserve(rhs.args.size());

    for (ProgramBodyTerm& arg : rhs.args)
    {
        argTerms.push_back(move(arg.term));
    }
    UTerm term = make_unique<FunctionTerm>(rhs.uid, rhs.name, move(argTerms), true);
    return ProgramBodyTerm(move(term));
}

inline ProgramOpArgument operator!(ProgramOpArgument&& lhs)
{
    UTerm term = make_unique<UnaryOpTerm>(EUnaryOperatorType::Negate, move(lhs.term));
    return ProgramOpArgument(move(term));
}

inline ProgramOpArgument operator<(ProgramOpArgument&& lhs, ProgramOpArgument&& rhs)
{
    UTerm term = make_unique<BinaryOpTerm>(EBinaryOperatorType::LessThan, move(lhs.term), move(rhs.term));
    return ProgramOpArgument(move(term));
}

inline ProgramOpArgument operator<=(ProgramOpArgument&& lhs, ProgramOpArgument&& rhs)
{
    UTerm term = make_unique<BinaryOpTerm>(EBinaryOperatorType::LessThanEq, move(lhs.term), move(rhs.term));
    return ProgramOpArgument(move(term));
}

inline ProgramOpArgument operator>(ProgramOpArgument&& lhs, ProgramOpArgument&& rhs)
{
    UTerm term = make_unique<BinaryOpTerm>(EBinaryOperatorType::GreaterThan, move(lhs.term), move(rhs.term));
    return ProgramOpArgument(move(term));
}

inline ProgramOpArgument operator>=(ProgramOpArgument&& lhs, ProgramOpArgument&& rhs)
{
    UTerm term = make_unique<BinaryOpTerm>(EBinaryOperatorType::GreaterThanEq, move(lhs.term), move(rhs.term));
    return ProgramOpArgument(move(term));
}

inline ProgramOpArgument operator==(ProgramOpArgument&& lhs, ProgramOpArgument&& rhs)
{
    UTerm term = make_unique<BinaryOpTerm>(EBinaryOperatorType::Equality, move(lhs.term), move(rhs.term));
    return ProgramOpArgument(move(term));
}

inline ProgramOpArgument operator!=(ProgramOpArgument&& lhs, ProgramOpArgument&& rhs)
{
    UTerm term = make_unique<BinaryOpTerm>(EBinaryOperatorType::Inequality, move(lhs.term), move(rhs.term));
    return ProgramOpArgument(move(term));
}

inline ProgramOpArgument operator*(ProgramOpArgument&& lhs, ProgramOpArgument&& rhs)
{
    UTerm term = make_unique<BinaryOpTerm>(EBinaryOperatorType::Multiply, move(lhs.term), move(rhs.term));
    return ProgramOpArgument(move(term));
}

inline ProgramOpArgument operator/(ProgramOpArgument&& lhs, ProgramOpArgument&& rhs)
{
    UTerm term = make_unique<BinaryOpTerm>(EBinaryOperatorType::Divide, move(lhs.term), move(rhs.term));
    return ProgramOpArgument(move(term));
}

inline ProgramOpArgument operator+(ProgramOpArgument&& lhs, ProgramOpArgument&& rhs)
{
    UTerm term = make_unique<BinaryOpTerm>(EBinaryOperatorType::Add, move(lhs.term), move(rhs.term));
    return ProgramOpArgument(move(term));
}

inline ProgramOpArgument operator-(ProgramOpArgument&& lhs, ProgramOpArgument&& rhs)
{
    UTerm term = make_unique<BinaryOpTerm>(EBinaryOperatorType::Subtract, move(lhs.term), move(rhs.term));
    return ProgramOpArgument(move(term));
}

inline ProgramHeadDisjunctionTerm operator|(ProgramHeadTerm&& lhs, ProgramHeadTerm&& rhs)
{
    lhs.bound = true;
    rhs.bound = true;

    vector<UTerm> children;
    children.reserve(2);
    children.push_back(move(lhs.term));
    children.push_back(move(rhs.term));
    return ProgramHeadDisjunctionTerm(make_unique<DisjunctionTerm>(move(children)));
}

inline ProgramHeadDisjunctionTerm& operator|(ProgramHeadDisjunctionTerm& lhs, ProgramHeadTerm&& rhs)
{
    rhs.bound = true;
    lhs.add(move(rhs.term));
    return lhs;
}

inline void operator<<=(ProgramHeadTerm&& head, ProgramBodyTerms&& body)
{
    head.bound = true;

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

inline void operator<<=(ProgramHeadDisjunctionTerm&& head, ProgramBodyTerm&& body)
{
    head.bound = true;

    vector<UTerm> terms;
    terms.push_back(move(body.term));
    operator<<=(ProgramHeadTerm(move(head.term)), ProgramBodyTerms(move(terms)));
}

inline void operator<<=(ProgramHeadDisjunctionTerm&& head, ProgramBodyTerms&& body)
{
    head.bound = true;

    operator<<=(ProgramHeadTerm(move(head.term)), move(body));
}

inline void operator<<=(ProgramHeadChoiceTerm&& head, ProgramBodyTerm&& body)
{
    head.bound = true;

    vector<UTerm> terms;
    terms.push_back(move(body.term));
    operator<<=(ProgramHeadTerm(move(head.term)), ProgramBodyTerms(move(terms)));
}

inline void operator<<=(ProgramHeadChoiceTerm&& head, ProgramBodyTerms&& body)
{
    head.bound = true;

    operator<<=(ProgramHeadTerm(move(head.term)), move(body));
}

}