// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once

#include "ConstraintTypes.h"
#include <EASTL/variant.h>

namespace Vertexy
{

enum class EUnaryOperatorType
{
    Negate
};

enum class EBinaryOperatorType
{
    LessThan,
    LessThanEq,
    GreaterThan,
    GreaterThanEq,
    Equality,
    Inequality,
    Multiply,
    Divide,
    Add,
    Subtract
};

enum VariableUID : int32_t { };
enum FormulaUID : int32_t { };

// Represents an ungrounded variable within a rule program
class ProgramVariable
{
public:
    ProgramVariable(const wchar_t* name=nullptr);
private:
    const wchar_t* m_name;
    VariableUID m_uid = VariableUID(0);
};

// ProgramSymbol represents a ground value. Can be either a int or (case-sensitive) string.
using ProgramSymbol = variant<int, const wchar_t*>;

class Term
{
public:
    virtual ~Term() {}
    virtual void visit(const function<void(Term*)>& visitor) = 0;
};

using UTerm = unique_ptr<Term>;
using STerm = shared_ptr<Term>;

class UnaryOpTerm : public Term
{
public:
    UnaryOpTerm(EUnaryOperatorType op, UTerm&& child)
        : op(op)
        , child(move(child))
    {
    }

    virtual void visit(const function<void(Term*)>& visitor) override
    {
        child->visit(visitor);
        visitor(this);
    }

    EUnaryOperatorType op;
    UTerm child;
};

class BinaryOpTerm : public Term
{
public:
    BinaryOpTerm(EBinaryOperatorType op, UTerm&& lhs, UTerm&& rhs)
        : op(op)
        , lhs(move(lhs))
        , rhs(move(rhs))
    {
    }

    virtual void visit(const function<void(Term*)>& visitor) override
    {
        lhs->visit(visitor);
        rhs->visit(visitor);
        visitor(this);
    }

    EBinaryOperatorType op;
    UTerm lhs;
    UTerm rhs;
};

class FunctionTerm : public Term
{
public:
    FunctionTerm(FormulaUID functionUID, const wchar_t* functionName, vector<UTerm>&& arguments, bool negated)
        : functionUID(functionUID)
        , functionName(functionName)
        , arguments(move(arguments))
        , negated(negated)
    {
    }

    virtual void visit(const function<void(Term*)>& visitor) override
    {
        visitor(this);
    }

    FormulaUID functionUID;
    const wchar_t* functionName;
    vector<UTerm> arguments;
    bool negated;
};

class VariableTerm : public Term
{
public:
    VariableTerm(ProgramVariable param) : param(param) {}

    virtual void visit(const function<void(Term*)>& visitor) override
    {
        visitor(this);
    }

    ProgramVariable param;
};

class SymbolTerm : public Term
{
public:
    SymbolTerm(const ProgramSymbol& sym) : sym(sym) {}

    virtual void visit(const function<void(Term*)>& visitor) override
    {
        visitor(this);
    }

    ProgramSymbol sym;
};

class DisjunctionTerm : public Term
{
public:
    DisjunctionTerm(vector<UTerm>&& children) : children(move(children)) {}

    virtual void visit(const function<void(Term*)>& visitor) override
    {
        for (auto& child : children)
        {
            child->visit(visitor);
        }
        visitor(this);
    }

    vector<UTerm> children;
};

class ChoiceTerm : public Term
{
public:
    ChoiceTerm(UTerm&& term) : subTerm(move(term)) {}

    virtual void visit(const function<void(Term*)>& visitor) override
    {
        subTerm->visit(visitor);
        visitor(this);
    }

    UTerm subTerm;
};

class RuleStatement
{
public:
    RuleStatement(UTerm&& head, vector<UTerm>&& body) : head(move(head)), body(move(body)) {}
    RuleStatement(UTerm&& head) : head(move(head)) {}

    template<typename T=Term>
    void visit(const function<void(T*)>& visitor)
    {
        visitHead<T>(visitor);
        visitBody<T>(visitor);
    }

    template<typename T=Term>
    void visitHead(const function<void(T*)>& visitor)
    {
        head->visit([&](Term* term)
        {
           if (auto f = dynamic_cast<T*>(term))
           {
               visitor(f);
           }
        });
    }

    template<typename T>
    void visitBody(const function<void(T*)>& visitor)
    {
        for (auto& bodyTerm : body)
        {
            bodyTerm->visit([&](Term* term)
            {
                if (auto f = dynamic_cast<T*>(term))
                {
                    visitor(f);
                }
            });
        }
    }

    UTerm head;
    vector<UTerm> body;
};

using URuleStatement = unique_ptr<RuleStatement>;

};