// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once
#include "ConstraintTypes.h"
#include "program/Program.h"
#include "program/ProgramAST.h"

#define VXY_VARIABLE(name) ProgramVariable name(L#name)
#define VXY_FORMULA(name, arity) Formula<arity> name(L#name)

/**
 * Implementation of the mini language for defining rules inside a Program::define() block
 */
namespace Vertexy
{

template<int ARITY> class Formula;
template<int ARITY> class ExternalFormula;
template<int ARITY> class FormulaResult;

//
// Internal types for enforcing language rules
//
namespace detail
{
    class ProgramFunctionTerm;
    class ProgramHeadTerm;
    class ProgramBodyTerm;
    class ProgramBodyTerms;

    class ProgramOpArgument
    {
    public:
        ProgramOpArgument(int constant)
        {
            term = make_unique<SymbolTerm>(ProgramSymbol(constant));
        }
        ProgramOpArgument(ProgramVertex vert)
        {
            term = make_unique<VertexTerm>();
        }
        ProgramOpArgument(const ProgramSymbol& sym)
        {
            term = make_unique<SymbolTerm>(sym);
        }
        ProgramOpArgument(ProgramVariable param)
        {
            term = make_unique<VariableTerm>(param);
        }

        explicit ProgramOpArgument(ULiteralTerm&& term) : term(move(term))
        {
        }

        ULiteralTerm term;
    };

    class ProgramRangeTerm
    {
    public:
        ProgramRangeTerm(int min, int max) : min(min), max(max) {};
        int min = 0, max = -1;
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

        UHeadTerm term;
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

        void add(UFunctionHeadTerm&& child)
        {
            static_cast<DisjunctionTerm*>(term.get())->children.push_back(move(child));
        }

        UHeadTerm term;
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
        UFunctionHeadTerm createHeadTerm();
    };


    class ProgramExternalFunctionTerm
    {
    public:
        explicit ProgramExternalFunctionTerm(FormulaUID uid, const wchar_t* name, const IExternalFormulaProviderPtr& provider, vector<ProgramBodyTerm>&& args)
            : uid(uid)
            , name(name)
            , args(move(args))
            , provider(provider)
        {
        }

        FormulaUID uid;
        const wchar_t* name;
        vector<ProgramBodyTerm> args;
        IExternalFormulaProviderPtr provider;
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
        ProgramBodyTerm(ProgramVertex vert)
        {
            term = make_unique<VertexTerm>();
        }
        ProgramBodyTerm(ProgramFunctionTerm&& f)
        {
            f.bound = true;

            vector<ULiteralTerm> argTerms;
            argTerms.reserve(f.args.size());

            for (ProgramBodyTerm& arg : f.args)
            {
                argTerms.push_back(move(arg.term));
            }
            term = make_unique<FunctionTerm>(f.uid, f.name, move(argTerms), false, nullptr);
        }
        ProgramBodyTerm(ProgramExternalFunctionTerm&& f)
        {
            vector<ULiteralTerm> argTerms;
            argTerms.reserve(f.args.size());

            for (ProgramBodyTerm& arg : f.args)
            {
                argTerms.push_back(move(arg.term));
            }
            term = make_unique<FunctionTerm>(f.uid, f.name, move(argTerms), false, f.provider);
        }
        ProgramBodyTerm(ProgramOpArgument&& h)
        {
            term = move(h.term);
        }

        explicit ProgramBodyTerm(ULiteralTerm&& inTerm)
        {
            term = move(inTerm);
        }

        ULiteralTerm term;
    };

    class ProgramBodyTerms
    {
    public:
        explicit ProgramBodyTerms(vector<ULiteralTerm>&& inTerms) : terms(move(inTerms))
        {
        };
        ProgramBodyTerms(ProgramBodyTerm&& rhs)
        {
            terms.push_back(move(rhs.term));
        }
        void add(ULiteralTerm&& child)
        {
            terms.push_back(move(child));
        }

        vector<ULiteralTerm> terms;
    };

    class ProgramHeadTerm
    {
    public:
        ProgramHeadTerm(ProgramFunctionTerm&& f)
        {
            bound = false;
            f.bound = true;

            vector<ULiteralTerm> argTerms;
            argTerms.reserve(f.args.size());

            for (ProgramBodyTerm& arg : f.args)
            {
                argTerms.push_back(move(arg.term));
            }
            term = make_unique<FunctionHeadTerm>(f.uid, f.name, move(argTerms));
        }

        explicit ProgramHeadTerm(UFunctionHeadTerm&& term) : term(move(term))
        {
        }

        ~ProgramHeadTerm();

        UFunctionHeadTerm term;
        bool bound = false;
    };

} // namespace Detail

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
    static tuple<UProgramInstance, R> runDefinition(const ProgramDefinitionFunctor<R, ARGS...>& fn, ARGS&&... args)
    {
        auto inst = make_unique<ProgramInstance>();
        vxy_assert_msg(s_currentInstance == nullptr, "Cannot define two programs simultaneously!");
        s_currentInstance = inst.get();

        tuple<UProgramInstance, R> out;
        if constexpr (is_same_v<R, void>)
        {
            fn(forward<ARGS>(args)...);
            out = make_tuple(move(inst), (void)0);
        }
        else
        {
            R result = fn(forward<ARGS>(args)...);
            out = make_tuple(move(inst), move(result));
        }

        s_currentInstance = nullptr;
        return out;
    }

    // Specialization for passing in a topology for graph instantiation of programs.
    // Note that (currently) only the first argument can be a graph-instantiated.
    // The definition function should take a ProgramVertex in place of this argument, which represents
    // any vertex on the graph.
    template<typename R, typename... ARGS>
    static tuple<UProgramInstance, R> runDefinition(const ProgramDefinitionFunctor<R, ProgramVertex, ARGS...>& fn, const ITopologyPtr& topo, ARGS&&... args)
    {
        auto inst = make_unique<ProgramInstance>(topo);
        vxy_assert_msg(s_currentInstance == nullptr, "Cannot define two programs simultaneously!");
        s_currentInstance = inst.get();

        tuple<UProgramInstance, R> out;
        if constexpr (is_same_v<R, void>)
        {
            fn(ProgramVertex(), forward<ARGS>(args)...);
            out = make_tuple(move(inst), (void)0);
        }
        else
        {
            R result = fn(ProgramVertex(), forward<ARGS>(args)...);
            out = make_tuple(move(inst), move(result));
        }

        s_currentInstance = nullptr;
        return out;
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

    static void disallow(detail::ProgramBodyTerm&& body);
    static void disallow(detail::ProgramBodyTerms&& body);

    static detail::ProgramRangeTerm range(detail::ProgramBodyTerm min, detail::ProgramBodyTerm max);

    static ExternalFormula<2> graphLink(const TopologyLink& link);
    static detail::ProgramExternalFunctionTerm graphEdge(detail::ProgramBodyTerm&& left, detail::ProgramBodyTerm&& right);
    static detail::ProgramExternalFunctionTerm vertex(detail::ProgramBodyTerm&& term);

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

    // "parse" the definition, returning the ProgramInstance.
    inline tuple<UProgramInstance, R> apply(ARGS&&... args)
    {
        return Program::runDefinition<R, ARGS...>(m_definition, forward<ARGS>(args)...);
    }

    inline tuple<UProgramInstance, R> operator()(ARGS&&... args)
    {
        return apply(eastl::move(args)...);
    }

    template<typename... REMARGS>
    inline tuple<UProgramInstance, R> operator()(const ITopologyPtr& topology, REMARGS&&... args)
    {
        static_assert(sizeof...(REMARGS) == sizeof...(ARGS)-1, "Incorrect number of arguments");
        return Program::runDefinition<R, REMARGS...>(m_definition, topology, forward<REMARGS>(args)...);
    }

protected:
    ProgramDefinitionFunctor<R, ARGS...> m_definition;
};

// A formula of a given arity. Formulas form the heads and bodies of rules.
template<int ARITY>
class Formula
{
    friend FormulaResult;

public:
    Formula(const wchar_t* name=nullptr)
        : m_name(name)
        , m_uid(Program::allocateFormulaUID())
    {
    }
    Formula(const Formula& rhs) = delete;
    Formula(Formula&& rhs) noexcept
        : m_name(rhs.m_name)
        , m_uid(rhs.m_uid)
    {
    }

    Formula& operator=(Formula&& rhs) noexcept
    {
        m_uid = rhs.m_uid;
        return *this;
    }
    Formula& operator=(const Formula& rhs) = delete;

    Formula& operator=(detail::ProgramRangeTerm&& rhs)
    {
        for (int i = rhs.min; i <= rhs.max; ++i)
        {
            operator()(i);
        }
        return *this;
    }

    template<typename... ARGS>
    detail::ProgramFunctionTerm operator()(ARGS&&... args)
    {
        static_assert(sizeof...(args) == ARITY, "Wrong number of arguments for formula");
        vxy_assert_msg(Program::getCurrentInstance() != nullptr, "Cannot apply a Formula outside of a Program::define block!");

        vector<detail::ProgramBodyTerm> fargs;
        fargs.reserve(ARITY);

        return foldArgs(fargs, args...);
    }

    FormulaUID getUID() const { return m_uid; }
    const wchar_t* getName() const { return m_name; }

private:
    template<typename T, typename... REM>
    detail::ProgramFunctionTerm foldArgs(vector<detail::ProgramBodyTerm>& outArgs, T&& arg, REM&&... rem)
    {
        outArgs.push_back(move(arg));
        return foldArgs(outArgs, rem...);
    }

    detail::ProgramFunctionTerm foldArgs(vector<detail::ProgramBodyTerm>& outArgs)
    {
        return detail::ProgramFunctionTerm(m_uid, m_name, move(outArgs));
    }

    const wchar_t* m_name;
    FormulaUID m_uid;
};


template<int ARITY>
class ExternalFormula
{
public:
    ExternalFormula(FormulaUID uid, const IExternalFormulaProviderPtr& provider, const wchar_t* name=nullptr)
        : m_uid(uid)
        , m_name(name)
        , m_provider(provider)
    {
    }

    template<typename... ARGS>
    detail::ProgramBodyTerm operator()(ARGS&&... args)
    {
        vector<detail::ProgramBodyTerm> fargs;
        fargs.reserve(ARITY);

        return foldArgs(fargs, args...);
    }

private:
    template<typename T, typename... REM>
    detail::ProgramBodyTerm foldArgs(vector<detail::ProgramBodyTerm>& outArgs, T&& arg, REM&&... rem)
    {
        outArgs.push_back(move(arg));
        return foldArgs(outArgs, rem...);
    }

    detail::ProgramBodyTerm foldArgs(vector<detail::ProgramBodyTerm>& outArgs)
    {
        return detail::ProgramBodyTerm(detail::ProgramExternalFunctionTerm(m_uid, m_name, m_provider, move(outArgs)));
    }

    FormulaUID m_uid;
    const wchar_t* m_name;
    IExternalFormulaProviderPtr m_provider;
};

// Allows returning a formula outside of the Program::define() block. The bind() function allows
// you to bind the formula atoms to existing variables.
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

    // Provide a function that given a specific instantiation of this formula, returns a SignedClause representing
    // the solver variable + value that should be linked with the truth of this formula.
    void bind(typename FormulaBinder<SignedClause, ARITY>::type&& binder)
    {
        vxy_assert_msg(m_instance && m_formulaUID >= 0, "FormulaResult not bound to a formula");
        m_instance->addBinder(m_formulaUID, eastl::make_unique<TBindClauseCaller<ARITY>>(eastl::move(binder)));
    }

    // Provide a function that given a specific instantiation of this formula, returns a VarID that should be linked
    // with the truth of this formula. The variable must be a boolean (i.e. have a domain size of exactly 2).
    void bind(typename FormulaBinder<VarID, ARITY>::type&& binder)
    {
        vxy_assert_msg(m_instance && m_formulaUID >= 0, "FormulaResult not bound to a formula");
        m_instance->addBinder(m_formulaUID, eastl::make_unique<TBindVarCaller<ARITY>>(eastl::move(binder)));
    }

    template<typename... ARGS>
    wstring toString(ARGS&&... args)
    {
        static_assert(sizeof...(args) == ARITY, "Wrong number of toString arguments");
        vector<ProgramSymbol> argVec;
        argVec.reserve(ARITY);
        return toStringHelper(move(argVec), args...);
    }

private:
    template<typename... ARGS>
    wstring toStringHelper(vector<ProgramSymbol>&& args, const ProgramSymbol& sym, ARGS&&... rem)
    {
        args.push_back(sym);
        return toStringHelper(move(args), rem...);
    }
    wstring toStringHelper(vector<ProgramSymbol>&& args)
    {
        ProgramSymbol formulaSym(m_formulaUID, m_formulaName, args, false);
        return formulaSym.toString();
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

inline detail::ProgramHeadChoiceTerm detail::ProgramFunctionTerm::choice()
{
    bound = true;
    auto choiceTerm = make_unique<ChoiceTerm>(createHeadTerm());
    return ProgramHeadChoiceTerm(move(choiceTerm));
}

inline UFunctionHeadTerm detail::ProgramFunctionTerm::createHeadTerm()
{
    vector<ULiteralTerm> argTerms;
    argTerms.reserve(args.size());

    for (ProgramBodyTerm& arg : args)
    {
        argTerms.push_back(move(arg.term));
    }
    return make_unique<FunctionHeadTerm>(uid, name, move(argTerms));
}

inline detail::ProgramFunctionTerm::~ProgramFunctionTerm()
{
    if (!bound)
    {
        // fact
        vxy_assert_msg(Program::getCurrentInstance() != nullptr, "Cannot specify rules outside of a Program::define block!");
        auto rule = make_unique<RuleStatement>(createHeadTerm());
        Program::getCurrentInstance()->addRule(move(rule));
    }
}

inline detail::ProgramHeadChoiceTerm::~ProgramHeadChoiceTerm()
{
    if (!bound)
    {
        // fact
        vxy_assert_msg(Program::getCurrentInstance() != nullptr, "Cannot specify rules outside of a Program::define block!");
        auto rule = make_unique<RuleStatement>(move(term));
        Program::getCurrentInstance()->addRule(move(rule));
    }
}

inline detail::ProgramHeadDisjunctionTerm::~ProgramHeadDisjunctionTerm()
{
    if (!bound)
    {
        // fact
        vxy_assert_msg(Program::getCurrentInstance() != nullptr, "Cannot specify rules outside of a Program::define block!");
        auto rule = make_unique<RuleStatement>(move(term));
        Program::getCurrentInstance()->addRule(move(rule));
    }
}

inline detail::ProgramHeadTerm::~ProgramHeadTerm()
{
    if (!bound)
    {
        // fact
        vxy_assert_msg(Program::getCurrentInstance() != nullptr, "Cannot specify rules outside of a Program::define block!");
        auto rule = make_unique<RuleStatement>(move(term));
        Program::getCurrentInstance()->addRule(move(rule));
    }
}

inline detail::ProgramBodyTerms operator&&(detail::ProgramBodyTerm&& lhs, detail::ProgramBodyTerm&& rhs)
{
    vector<ULiteralTerm> terms;
    terms.reserve(2);
    terms.push_back(move(lhs.term));
    terms.push_back(move(rhs.term));
    return detail::ProgramBodyTerms(move(terms));
}

inline detail::ProgramBodyTerms operator&&(detail::ProgramBodyTerms&& lhs, detail::ProgramBodyTerm&& rhs)
{
    vector<ULiteralTerm> terms = move(lhs.terms);
    terms.push_back(move(rhs.term));
    return detail::ProgramBodyTerms(move(terms));
}

inline detail::ProgramBodyTerm operator~(detail::ProgramFunctionTerm&& rhs)
{
    rhs.bound = true;

    vector<ULiteralTerm> argTerms;
    argTerms.reserve(rhs.args.size());

    for (detail::ProgramBodyTerm& arg : rhs.args)
    {
        argTerms.push_back(move(arg.term));
    }
    auto term = make_unique<FunctionTerm>(rhs.uid, rhs.name, move(argTerms), true, nullptr);
    return detail::ProgramBodyTerm(move(term));
}

inline detail::ProgramBodyTerm operator~(detail::ProgramExternalFunctionTerm&& rhs)
{
    vector<ULiteralTerm> argTerms;
    argTerms.reserve(rhs.args.size());

    for (detail::ProgramBodyTerm& arg : rhs.args)
    {
        argTerms.push_back(move(arg.term));
    }
    auto term = make_unique<FunctionTerm>(rhs.uid, rhs.name, move(argTerms), true, rhs.provider);
    return detail::ProgramBodyTerm(move(term));
}

inline detail::ProgramOpArgument operator!(detail::ProgramOpArgument&& lhs)
{
    auto term = make_unique<UnaryOpTerm>(EUnaryOperatorType::Negate, move(lhs.term));
    return detail::ProgramOpArgument(move(term));
}

inline detail::ProgramOpArgument operator<(detail::ProgramOpArgument&& lhs, detail::ProgramOpArgument&& rhs)
{
    auto term = make_unique<BinaryOpTerm>(EBinaryOperatorType::LessThan, move(lhs.term), move(rhs.term));
    return detail::ProgramOpArgument(move(term));
}

inline detail::ProgramOpArgument operator<=(detail::ProgramOpArgument&& lhs, detail::ProgramOpArgument&& rhs)
{
    auto term = make_unique<BinaryOpTerm>(EBinaryOperatorType::LessThanEq, move(lhs.term), move(rhs.term));
    return detail::ProgramOpArgument(move(term));
}

inline detail::ProgramOpArgument operator>(detail::ProgramOpArgument&& lhs, detail::ProgramOpArgument&& rhs)
{
    auto term = make_unique<BinaryOpTerm>(EBinaryOperatorType::GreaterThan, move(lhs.term), move(rhs.term));
    return detail::ProgramOpArgument(move(term));
}

inline detail::ProgramOpArgument operator>=(detail::ProgramOpArgument&& lhs, detail::ProgramOpArgument&& rhs)
{
    auto term = make_unique<BinaryOpTerm>(EBinaryOperatorType::GreaterThanEq, move(lhs.term), move(rhs.term));
    return detail::ProgramOpArgument(move(term));
}

inline detail::ProgramOpArgument operator==(detail::ProgramOpArgument&& lhs, detail::ProgramOpArgument&& rhs)
{
    auto term = make_unique<BinaryOpTerm>(EBinaryOperatorType::Equality, move(lhs.term), move(rhs.term));
    return detail::ProgramOpArgument(move(term));
}

inline detail::ProgramOpArgument operator!=(detail::ProgramOpArgument&& lhs, detail::ProgramOpArgument&& rhs)
{
    auto term = make_unique<BinaryOpTerm>(EBinaryOperatorType::Inequality, move(lhs.term), move(rhs.term));
    return detail::ProgramOpArgument(move(term));
}

inline detail::ProgramOpArgument operator*(detail::ProgramOpArgument&& lhs, detail::ProgramOpArgument&& rhs)
{
    auto term = make_unique<BinaryOpTerm>(EBinaryOperatorType::Multiply, move(lhs.term), move(rhs.term));
    return detail::ProgramOpArgument(move(term));
}

inline detail::ProgramOpArgument operator/(detail::ProgramOpArgument&& lhs, detail::ProgramOpArgument&& rhs)
{
    auto term = make_unique<BinaryOpTerm>(EBinaryOperatorType::Divide, move(lhs.term), move(rhs.term));
    return detail::ProgramOpArgument(move(term));
}

inline detail::ProgramOpArgument operator+(detail::ProgramOpArgument&& lhs, detail::ProgramOpArgument&& rhs)
{
    auto term = make_unique<BinaryOpTerm>(EBinaryOperatorType::Add, move(lhs.term), move(rhs.term));
    return detail::ProgramOpArgument(move(term));
}

inline detail::ProgramOpArgument operator-(detail::ProgramOpArgument&& lhs, detail::ProgramOpArgument&& rhs)
{
    auto term = make_unique<BinaryOpTerm>(EBinaryOperatorType::Subtract, move(lhs.term), move(rhs.term));
    return detail::ProgramOpArgument(move(term));
}

inline detail::ProgramHeadDisjunctionTerm operator|(detail::ProgramHeadTerm&& lhs, detail::ProgramHeadTerm&& rhs)
{
    lhs.bound = true;
    rhs.bound = true;

    vector<UFunctionHeadTerm> children;
    children.reserve(2);
    children.push_back(move(lhs.term));
    children.push_back(move(rhs.term));
    return detail::ProgramHeadDisjunctionTerm(make_unique<DisjunctionTerm>(move(children)));
}

inline detail::ProgramHeadDisjunctionTerm& operator|(detail::ProgramHeadDisjunctionTerm& lhs, detail::ProgramHeadTerm&& rhs)
{
    rhs.bound = true;
    lhs.add(move(rhs.term));
    return lhs;
}

inline void operator<<=(detail::ProgramHeadTerm&& head, detail::ProgramBodyTerms&& body)
{
    head.bound = true;

    vxy_assert_msg(Program::getCurrentInstance() != nullptr, "Cannot specify rules outside of a Program::define block!");
    auto rule = make_unique<RuleStatement>(move(head.term), move(body.terms));
    Program::getCurrentInstance()->addRule(move(rule));
}

inline void operator<<=(detail::ProgramHeadTerm&& head, detail::ProgramBodyTerm&& body)
{
    vector<ULiteralTerm> terms;
    terms.push_back(move(body.term));
    operator<<=(forward<detail::ProgramHeadTerm>(head), detail::ProgramBodyTerms(move(terms)));
}

inline void operator<<=(detail::ProgramHeadDisjunctionTerm&& head, detail::ProgramBodyTerms&& body)
{
    head.bound = true;

    vxy_assert_msg(Program::getCurrentInstance() != nullptr, "Cannot specify rules outside of a Program::define block!");
    auto rule = make_unique<RuleStatement>(move(head.term), move(body.terms));
    Program::getCurrentInstance()->addRule(move(rule));
}

inline void operator<<=(detail::ProgramHeadDisjunctionTerm&& head, detail::ProgramBodyTerm&& body)
{
    head.bound = true;

    vector<ULiteralTerm> terms;
    terms.push_back(move(body.term));
    operator<<=(forward<detail::ProgramHeadDisjunctionTerm>(head), detail::ProgramBodyTerms(move(terms)));
}

inline void operator<<=(detail::ProgramHeadChoiceTerm&& head, detail::ProgramBodyTerms&& body)
{
    head.bound = true;

    vxy_assert_msg(Program::getCurrentInstance() != nullptr, "Cannot specify rules outside of a Program::define block!");
    auto rule = make_unique<RuleStatement>(move(head.term), move(body.terms));
    Program::getCurrentInstance()->addRule(move(rule));
}

inline void operator<<=(detail::ProgramHeadChoiceTerm&& head, detail::ProgramBodyTerm&& body)
{
    head.bound = true;

    vector<ULiteralTerm> terms;
    terms.push_back(move(body.term));
    operator<<=(forward<detail::ProgramHeadChoiceTerm>(head), detail::ProgramBodyTerms(move(terms)));
}

} // namespace Vertexy