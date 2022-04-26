// Copyright Proletariat, Inc. All Rights Reserved.
#include "program/Program.h"

#include "program/ProgramDSL.h"

using namespace Vertexy;

ProgramInstanceBase* Program::s_currentInstance = nullptr;
int Program::s_nextFormulaUID = 1;
int Program::s_nextParameterUID = 1;

ProgramParameter::ProgramParameter(const wchar_t* name)
    : m_name(name)
{
    vxy_assert_msg(Program::getCurrentInstance() != nullptr, "Cannot define a ProgramParameter outside of a Program::define block!");
    m_uid = Program::allocateParameterUID();
}

void Program::disallow(ProgramBodyTerm&& body)
{
    return disallow(ProgramBodyTerms(forward<ProgramBodyTerm>(body)));
}

void Program::disallow(ProgramBodyTerms&& body)
{
    vxy_assert_msg(Program::getCurrentInstance() != nullptr, "Cannot specify rules outside of a Program::define block!");
    vector<UTerm> terms;
    auto rule = make_unique<RuleStatement>(nullptr, move(body.terms));
    s_currentInstance->addRule(move(rule));
}

Formula<1> Program::range(int min, int max)
{
    return Formula<1>(L"range");
}
