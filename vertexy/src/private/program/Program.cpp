// Copyright Proletariat, Inc. All Rights Reserved.
#include "program/Program.h"

#include "program/ProgramDSL.h"

using namespace Vertexy;

ProgramInstance* Program::s_currentInstance = nullptr;
int Program::s_nextFormulaUID = 1;
int Program::s_nextVarUID = 1;

ProgramVariable::ProgramVariable(const wchar_t* name)
    : m_name(name)
{
    m_uid = Program::allocateVariableUID();
}

void Program::disallow(detail::ProgramBodyTerm&& body)
{
    return disallow(detail::ProgramBodyTerms(forward<detail::ProgramBodyTerm>(body)));
}

void Program::disallow(detail::ProgramBodyTerms&& body)
{
    vxy_assert_msg(Program::getCurrentInstance() != nullptr, "Cannot specify rules outside of a Program::define block!");
    vector<UTerm> terms;
    auto rule = make_unique<RuleStatement>(nullptr, move(body.terms));
    s_currentInstance->addRule(move(rule));
}

Vertexy::detail::ProgramRangeTerm Program::range(ProgramSymbol min, ProgramSymbol max)
{
    int minV = min.getInt();
    int maxV = max.getInt();
    vxy_assert_msg(maxV >= minV, "invalid range");
    return detail::ProgramRangeTerm(minV, maxV);
}
