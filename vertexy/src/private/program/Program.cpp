// Copyright Proletariat, Inc. All Rights Reserved.
#include "program/Program.h"

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
