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
        explicit ProgramExternalFunctionTerm(const IExternalFormulaProviderPtr& provider, vector<ProgramBodyTerm>&& args)
            : provider(provider)
            , args(move(args))
        {
        }

        IExternalFormulaProviderPtr provider;
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
        ProgramBodyTerm(const ProgramVariable& p)
        {
            term = make_unique<VariableTerm>(p);
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
            term = make_unique<FunctionTerm>(f.uid, f.name, move(argTerms), false);
        }
        ProgramBodyTerm(ProgramExternalFunctionTerm&& f)
        {
            vector<ULiteralTerm> argTerms;
            argTerms.reserve(f.args.size());

            for (ProgramBodyTerm& arg : f.args)
            {
                argTerms.push_back(move(arg.term));
            }
            term = make_unique<ExternalFunctionTerm>(f.provider, move(argTerms), false);
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

    template<int ARITY>
    class ExternalFormula
    {
    public:
        ExternalFormula(const IExternalFormulaProviderPtr& provider, const wchar_t* name=nullptr)
            : m_name(name)
            , m_provider(provider)
        {
        }

        template<typename... ARGS>
        ProgramBodyTerm operator()(ARGS&&... args)
        {
            vector<ProgramBodyTerm> fargs;
            fargs.reserve(ARITY);

            return foldArgs(fargs, args...);
        }

    private:
        template<typename T, typename... REM>
        ProgramBodyTerm foldArgs(vector<ProgramBodyTerm>& outArgs, T&& arg, REM&&... rem)
        {
            outArgs.push_back(move(arg));
            return foldArgs(outArgs, rem...);
        }

        ProgramBodyTerm foldArgs(vector<ProgramBodyTerm>& outArgs)
        {
            return ProgramBodyTerm(ProgramExternalFunctionTerm(m_provider, move(outArgs)));
        }

        const wchar_t* m_name;
        IExternalFormulaProviderPtr m_provider;
    };

} // namespace Detail

// TProgramInstance returned from applying a ProgramDefinition (via the () operator)
template<typename RESULT>
class TProgramInstance : public ProgramInstance
{
public:
    TProgramInstance() {}

    // The typed result returned from the definition
    RESULT result;
};

// Specialization for ProgramDefinitions with no return values
template<>
class TProgramInstance<void> : public ProgramInstance {};

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

    static void disallow(detail::ProgramBodyTerm&& body);
    static void disallow(detail::ProgramBodyTerms&& body);

    static detail::ProgramRangeTerm range(ProgramSymbol min, ProgramSymbol max);

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
    auto term = make_unique<FunctionTerm>(rhs.uid, rhs.name, move(argTerms), true);
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
    auto term = make_unique<ExternalFunctionTerm>(rhs.provider, move(argTerms), true);
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