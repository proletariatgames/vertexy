// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once

#include "ConstraintTypes.h"
#include <EASTL/variant.h>

namespace  Vertexy
{

template<int ARITY> class Formula;
class FormulaAtom;
class HeadTerm;
class BodyTerm;

class FormulaAtom;


class FormulaParameter
{
    int32_t uid = 0;
};

class BodyTerm
{
public:
    BodyTerm(int constant);
    BodyTerm(const FormulaAtom& atom);
    BodyTerm(FormulaParameter param);
};

BodyTerm operator&&(const BodyTerm& lhs, const BodyTerm& rhs);
BodyTerm operator<(const BodyTerm& lhs, const BodyTerm& rhs);
BodyTerm operator<=(const BodyTerm& lhs, const BodyTerm& rhs);
BodyTerm operator>(const BodyTerm& lhs, const BodyTerm& rhs);
BodyTerm operator>=(const BodyTerm& lhs, const BodyTerm& rhs);
BodyTerm operator==(const BodyTerm& lhs, const BodyTerm& rhs);
BodyTerm operator!=(const BodyTerm& lhs, const BodyTerm& rhs);
BodyTerm operator*(const BodyTerm& lhs, const BodyTerm& rhs);
BodyTerm operator/(const BodyTerm& lhs, const BodyTerm& rhs);
BodyTerm operator+(const BodyTerm& lhs, const BodyTerm& rhs);
BodyTerm operator-(const BodyTerm& lhs, const BodyTerm& rhs);
BodyTerm operator-(const BodyTerm& rhs);

class HeadTerm
{
    HeadTerm operator|(const FormulaAtom& rhs) const;
};

class FormulaAtom
{
public:
    explicit FormulaAtom(vector<BodyTerm>&& args) : args(move(args)) {}

    HeadTerm operator|(const FormulaAtom& rhs) const;
    vector<BodyTerm> args;
};

void operator<<=(const HeadTerm& head, const BodyTerm& body);
void operator<<=(const FormulaAtom& head, const BodyTerm& body);

template<int ARITY>
class Formula
{
public:
    template<typename... ARGS>
    FormulaAtom operator()(ARGS&&... args)
    {
        static_assert(sizeof...(args) == ARITY, "Wrong number of arguments for formula");
        vector<BodyTerm> fargs;
        fargs.reserve(ARITY);

        foldArgs(fargs, args...);
        return FormulaAtom(move(fargs));
    }

private:
    template<typename T, typename... REM>
    void foldArgs(vector<BodyTerm>& outArgs, T& arg, REM&&... rem)
    {
        outArgs.push_back(arg);
        foldArgs(outArgs, rem...);
    }
    void foldArgs(vector<BodyTerm>& outArgs) {}
};


class Program
{
public:
    template<typename T>
    static Program define(T&& definition) { return Program(); }

    static void disallow(const BodyTerm& body);
    static Formula<1> range(int min, int max);
};


}