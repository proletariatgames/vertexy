// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once

#include "ConstraintTypes.h"
#include <EASTL/variant.h>

namespace Vertexy
{

class ProgramParameter
{
public:
    ProgramParameter();
private:
    int32_t m_uid = 0;
};

using ProgramSymbol = variant<int, wstring>;

class Term
{
public:
    virtual ~Term()
    {
        vxy_fail();
    }
};

using UTerm = unique_ptr<Term>;
using STerm = shared_ptr<Term>;

enum class EUnaryOperatorType
{
    Negate
};

class UnaryOpTerm : public Term
{
public:
    UnaryOpTerm(EUnaryOperatorType op, UTerm&& child)
        : op(op)
        , child(move(child))
    {
    }

    EUnaryOperatorType op;
    UTerm child;
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

class BinaryOpTerm : public Term
{
public:
    BinaryOpTerm(EBinaryOperatorType op, UTerm&& lhs, UTerm&& rhs)
        : op(op)
        , lhs(move(lhs))
        , rhs(move(rhs))
    {
    }

    EBinaryOperatorType op;
    UTerm lhs;
    UTerm rhs;
};

class FunctionTerm : public Term
{
public:
    FunctionTerm(int functionUID, vector<UTerm>&& arguments)
        : functionUID(functionUID)
        , arguments(move(arguments))
    {
    }

    int functionUID;
    vector<UTerm> arguments;
};

class VariableTerm : public Term
{
public:
    VariableTerm(ProgramParameter param) : param(param) {}

    ProgramParameter param;
};

class SymbolTerm : public Term
{
public:
    SymbolTerm(const ProgramSymbol& sym) : sym(sym) {}

    ProgramSymbol sym;
};

class DisjunctionTerm : public Term
{
public:
    DisjunctionTerm(vector<UTerm>&& children) : children(move(children)) {}

    vector<UTerm> children;
};

class ChoiceTerm : public Term
{
public:
    ChoiceTerm(UTerm&& term) : term(move(term)) {}

protected:
    UTerm term;
};

class RuleStatement
{
public:
    RuleStatement(UTerm&& head, vector<UTerm>&& body) : head(move(head)), body(move(body)) {}

    UTerm head;
    vector<UTerm> body;
};
typedef unique_ptr<RuleStatement> URuleStatement;

};