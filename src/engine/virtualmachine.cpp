// SPDX-License-Identifier: Apache-2.0

#include "virtualmachine.h"
#include "../scratch/list.h"
#include "../engine/engine.h"
#include <iostream>

#define MAX_REG_COUNT 1024

#define DISPATCH() goto *dispatch_table[*++pos]
#define FREE_REGS(count) m_regCount -= count
#define ADD_RET_VALUE(value) *m_regs[m_regCount++] = value
#define REPLACE_RET_VALUE(value, offset) *m_regs[m_regCount - offset] = value
#define GET_NEXT_ARG() m_constValues[*++pos]
#define READ_REG(index, count) m_regs[m_regCount - count + index]
#define READ_LAST_REG() m_regs[m_regCount - 1]

#define FIX_LIST_INDEX(index, listSize)                                                                                                                                                                \
    if ((listSize == 0) || (index < 1) || (index > listSize))                                                                                                                                          \
    index = 0

using namespace libscratchcpp;
using namespace vm;

static const double pi = std::acos(-1); // TODO: Use std::numbers::pi in C++20

/*! Constructs VirtualMachine. */
VirtualMachine::VirtualMachine()
{
}

/*! \copydoc VirtualMachine() */
VirtualMachine::VirtualMachine(Target *target, Engine *engine) :
    m_target(target),
    m_engine(engine)
{
    m_regs = new Value *[MAX_REG_COUNT];
    for (int i = 0; i < MAX_REG_COUNT; i++)
        m_regs[i] = new Value();
    m_loops.reserve(256);
    m_callTree.reserve(1024);
}

/*! Destroys the VirtualMachine object. */
VirtualMachine::~VirtualMachine()
{
    for (int i = 0; i < MAX_REG_COUNT; i++)
        delete m_regs[i];
    delete m_regs;
}

/*! Sets the list of procedures (custom blocks). */
void VirtualMachine::setProcedures(const std::vector<unsigned int *> &procedures)
{
    m_proceduresVector = procedures;
    m_procedures = m_proceduresVector.data();
}

/*! Sets the list of functions. */
void VirtualMachine::setFunctions(const std::vector<BlockFunc> &functions)
{
    m_functionsVector = functions;
    m_functions = m_functionsVector.data();
}

/*! Sets the list of constant values. */
void VirtualMachine::setConstValues(const std::vector<Value> &values)
{
    m_constValuesVector = values;
    m_constValues = m_constValuesVector.data();
}

/*! Sets the list of variables. */
void VirtualMachine::setVariables(const std::vector<Value *> &variables)
{
    m_variablesVector = variables;
    m_variables = m_variablesVector.data();
}

/*! Sets the list of lists. */
void VirtualMachine::setLists(const std::vector<List *> &lists)
{
    m_listsVector = lists;
    m_lists = m_listsVector.data();
}

/*! Sets the bytecode of the script. */
void VirtualMachine::setBytecode(const std::vector<unsigned int> &code)
{
    m_bytecodeVector = code;
    m_bytecode = m_bytecodeVector.data();
}

/*! Runs the script. */
unsigned int *VirtualMachine::run()
{
    return run(m_bytecode);
}

/*! Continues running the script from the given position (the first instruction is skipped). */
unsigned int *VirtualMachine::run(unsigned int *pos)
{
    static const void *dispatch_table[] = {
        nullptr,
        &&do_halt,
        &&do_const,
        &&do_null,
        &&do_if,
        &&do_else,
        &&do_endif,
        &&do_forever_loop,
        &&do_repeat_loop,
        &&do_until_loop,
        &&do_begin_until_loop,
        &&do_loop_end,
        &&do_print,
        &&do_add,
        &&do_subtract,
        &&do_multiply,
        &&do_divide,
        &&do_mod,
        &&do_random,
        &&do_round,
        &&do_abs,
        &&do_floor,
        &&do_ceil,
        &&do_sqrt,
        &&do_sin,
        &&do_cos,
        &&do_tan,
        &&do_asin,
        &&do_acos,
        &&do_atan,
        &&do_greater_than,
        &&do_less_than,
        &&do_equals,
        &&do_and,
        &&do_or,
        &&do_not,
        &&do_set_var,
        &&do_change_var,
        &&do_read_var,
        &&do_read_list,
        &&do_list_append,
        &&do_list_del,
        &&do_list_del_all,
        &&do_list_insert,
        &&do_list_replace,
        &&do_list_get_item,
        &&do_list_index_of,
        &&do_list_length,
        &&do_list_contains,
        &&do_str_concat,
        &&do_str_at,
        &&do_str_length,
        &&do_str_contains,
        &&do_exec,
        &&do_init_procedure,
        &&do_call_procedure,
        &&do_add_arg,
        &&do_read_arg,
        &&do_break_atomic
    };
    unsigned int *loopStart;
    unsigned int *loopEnd;
    size_t loopCount;
    m_atEnd = false;
    m_atomic = true;
    DISPATCH();

do_halt:
    if (m_regCount > 0) {
        std::cout << "warning: VM: " << m_regCount << " registers were leaked by the script; this is most likely a bug in the VM or in the compiler" << std::endl;
    }
    if (m_callTree.empty()) {
        m_atEnd = true;
        return pos;
    } else {
        pos = m_callTree.back();
        m_callTree.pop_back();
        m_procedureArgTree.pop_back();
        if (m_procedureArgTree.empty())
            m_procedureArgs = nullptr;
        else
            m_procedureArgs = &m_procedureArgTree.back();
        DISPATCH();
    }

do_const:
    ADD_RET_VALUE(GET_NEXT_ARG());
    DISPATCH();

do_null:
    ADD_RET_VALUE(Value());
    DISPATCH();

do_if:
    if (!READ_LAST_REG()->toBool()) {
        while (*pos != OP_ELSE && *pos != OP_ENDIF)
            pos += instruction_arg_count[*pos++];
    }
    FREE_REGS(1);
    DISPATCH();

do_else:
    while (*pos != OP_ENDIF)
        pos += instruction_arg_count[*pos++];

do_endif:
    DISPATCH();

do_forever_loop:
    Loop l;
    l.isRepeatLoop = true;
    l.start = pos;
    l.index = -1;
    m_loops.push_back(l);
    DISPATCH();

do_repeat_loop:
    loopCount = READ_LAST_REG()->toLong();
    FREE_REGS(1);
    if (loopCount <= 0) {
        loopEnd = pos;
        while (*loopEnd != OP_LOOP_END)
            loopEnd += instruction_arg_count[*loopEnd++];
        pos = loopEnd;
    } else {
        Loop l;
        l.isRepeatLoop = true;
        l.start = pos;
        l.index = 0;
        l.max = loopCount;
        m_loops.push_back(l);
    }
    DISPATCH();

do_until_loop:
    loopStart = run(pos);
    if (!READ_LAST_REG()->toBool()) {
        Loop l;
        l.isRepeatLoop = false;
        l.start = pos;
        m_loops.push_back(l);
        pos = loopStart;
    } else {
        pos = loopStart;
        while (*pos != OP_LOOP_END)
            pos += instruction_arg_count[*pos++];
    }
    FREE_REGS(1);
    DISPATCH();

do_begin_until_loop:
    return pos;

do_loop_end : {
    Loop &l = m_loops.back();
    if (l.isRepeatLoop) {
        if ((l.index == -1) || (++l.index < l.max))
            pos = l.start;
        else
            m_loops.pop_back();
        if (!m_atomic) {
            m_engine->breakFrame();
            return pos;
        }
        DISPATCH();
    } else {
        if (!m_atomic) {
            m_engine->breakFrame();
            return pos - 1;
        }
        loopStart = run(l.start);
        if (!READ_LAST_REG()->toBool())
            pos = loopStart;
        else
            m_loops.pop_back();
        FREE_REGS(1);
        DISPATCH();
    }
}

do_print:
    std::cout << READ_LAST_REG()->toString() << std::endl;
    FREE_REGS(1);
    DISPATCH();

do_add:
    REPLACE_RET_VALUE(*READ_REG(0, 2), 2);
    READ_REG(0, 2)->add(*READ_REG(1, 2));
    FREE_REGS(1);
    DISPATCH();

do_subtract:
    REPLACE_RET_VALUE(*READ_REG(0, 2), 2);
    READ_REG(0, 2)->subtract(*READ_REG(1, 2));
    FREE_REGS(1);
    DISPATCH();

do_multiply:
    REPLACE_RET_VALUE(*READ_REG(0, 2), 2);
    READ_REG(0, 2)->multiply(*READ_REG(1, 2));
    FREE_REGS(1);
    DISPATCH();

do_divide:
    REPLACE_RET_VALUE(*READ_REG(0, 2), 2);
    READ_REG(0, 2)->divide(*READ_REG(1, 2));
    FREE_REGS(1);
    DISPATCH();

do_mod:
    REPLACE_RET_VALUE(*READ_REG(0, 2), 2);
    READ_REG(0, 2)->mod(*READ_REG(1, 2));
    FREE_REGS(1);
    DISPATCH();

do_random:
    REPLACE_RET_VALUE(randint<long>(READ_REG(0, 2)->toDouble(), READ_REG(1, 2)->toDouble()), 2);
    FREE_REGS(1);
    DISPATCH();

do_round : {
    const Value *v = READ_REG(0, 1);
    if (!v->isInfinity() && !v->isNegativeInfinity())
        REPLACE_RET_VALUE(static_cast<long>(std::round(v->toDouble())), 1);
    DISPATCH();
}

do_abs : {
    const Value *v = READ_REG(0, 1);
    if (v->isNegativeInfinity())
        REPLACE_RET_VALUE(Value(Value::SpecialValue::Infinity), 1);
    else if (!v->isInfinity())
        REPLACE_RET_VALUE(std::abs(v->toDouble()), 1);
    DISPATCH();
}

do_floor : {
    const Value *v = READ_REG(0, 1);
    if (!v->isInfinity() && !v->isNegativeInfinity())
        REPLACE_RET_VALUE(std::floor(v->toDouble()), 1);
    DISPATCH();
}

do_ceil : {
    const Value *v = READ_REG(0, 1);
    if (!v->isInfinity() && !v->isNegativeInfinity())
        REPLACE_RET_VALUE(std::ceil(v->toDouble()), 1);
    DISPATCH();
}

do_sqrt : {
    const Value &v = *READ_REG(0, 1);
    if (v < 0)
        REPLACE_RET_VALUE(Value(Value::SpecialValue::NaN), 1);
    else if (!v.isInfinity())
        REPLACE_RET_VALUE(std::sqrt(v.toDouble()), 1);
    DISPATCH();
}

do_sin : {
    const Value *v = READ_REG(0, 1);
    if (v->isInfinity() || v->isNegativeInfinity())
        REPLACE_RET_VALUE(Value(Value::SpecialValue::NaN), 1);
    else
        REPLACE_RET_VALUE(std::sin(v->toDouble() * pi / 180), 1);
    DISPATCH();
}

do_cos : {
    const Value *v = READ_REG(0, 1);
    if (v->isInfinity() || v->isNegativeInfinity())
        REPLACE_RET_VALUE(Value(Value::SpecialValue::NaN), 1);
    else
        REPLACE_RET_VALUE(std::cos(v->toDouble() * pi / 180), 1);
    DISPATCH();
}

do_tan : {
    const Value *v = READ_REG(0, 1);
    if (v->isInfinity() || v->isNegativeInfinity())
        REPLACE_RET_VALUE(Value(Value::SpecialValue::NaN), 1);
    else {
        long mod = v->toLong() % 360;
        if (mod == 90)
            REPLACE_RET_VALUE(Value(Value::SpecialValue::Infinity), 1);
        else if (mod == 270)
            REPLACE_RET_VALUE(Value(Value::SpecialValue::NegativeInfinity), 1);
        else
            REPLACE_RET_VALUE(std::tan(v->toDouble() * pi / 180), 1);
    }
    DISPATCH();
}

do_asin : {
    const Value &v = *READ_REG(0, 1);
    if (v < -1 || v > 1)
        REPLACE_RET_VALUE(Value(Value::SpecialValue::NaN), 1);
    else
        REPLACE_RET_VALUE(std::asin(v.toDouble()) * 180 / pi, 1);
    DISPATCH();
}

do_acos : {
    const Value &v = *READ_REG(0, 1);
    if (v < -1 || v > 1)
        REPLACE_RET_VALUE(Value(Value::SpecialValue::NaN), 1);
    else
        REPLACE_RET_VALUE(std::acos(v.toDouble()) * 180 / pi, 1);
    DISPATCH();
}

do_atan : {
    const Value &v = *READ_REG(0, 1);
    if (v < -1 || v > 1)
        REPLACE_RET_VALUE(Value(Value::SpecialValue::NaN), 1);
    else
        REPLACE_RET_VALUE(std::atan(v.toDouble()) * 180 / pi, 1);
    DISPATCH();
}

do_greater_than:
    REPLACE_RET_VALUE(*READ_REG(0, 2) > *READ_REG(1, 2), 2);
    FREE_REGS(1);
    DISPATCH();

do_less_than:
    REPLACE_RET_VALUE(*READ_REG(0, 2) < *READ_REG(1, 2), 2);
    FREE_REGS(1);
    DISPATCH();

do_equals:
    REPLACE_RET_VALUE(*READ_REG(0, 2) == *READ_REG(1, 2), 2);
    FREE_REGS(1);
    DISPATCH();

do_and:
    REPLACE_RET_VALUE(READ_REG(0, 2)->toBool() && READ_REG(1, 2)->toBool(), 2);
    FREE_REGS(1);
    DISPATCH();

do_or:
    REPLACE_RET_VALUE(READ_REG(0, 2)->toBool() || READ_REG(1, 2)->toBool(), 2);
    FREE_REGS(1);
    DISPATCH();

do_not:
    REPLACE_RET_VALUE(!READ_LAST_REG()->toBool(), 1);
    DISPATCH();

do_set_var:
    *m_variables[*++pos] = *READ_LAST_REG();
    FREE_REGS(1);
    DISPATCH();

do_read_var:
    ADD_RET_VALUE(*m_variables[*++pos]);
    DISPATCH();

do_change_var:
    m_variables[*++pos]->add(*READ_LAST_REG());
    FREE_REGS(1);
    DISPATCH();

do_read_list:
    ADD_RET_VALUE(m_lists[*++pos]->toString());
    DISPATCH();

do_list_append:
    m_lists[*++pos]->push_back(*READ_LAST_REG());
    FREE_REGS(1);
    DISPATCH();

do_list_del : {
    const Value *indexValue = READ_LAST_REG();
    size_t index;
    List *list = m_lists[*++pos];
    if (indexValue->isString()) {
        const std::string &str = indexValue->toString();
        if (str == "last") {
            index = list->size();
        } else if (str == "all") {
            list->clear();
            index = 0;
        } else if (str == "random") {
            size_t size = list->size();
            index = size == 0 ? 0 : randint<size_t>(1, size);
        } else
            index = 0;
    } else {
        index = indexValue->toLong();
        FIX_LIST_INDEX(index, list->size());
    }
    if (index != 0)
        list->removeAt(index - 1);
    FREE_REGS(1);
    DISPATCH();
}

do_list_del_all:
    m_lists[*++pos]->clear();
    DISPATCH();

do_list_insert : {
    const Value *indexValue = READ_REG(1, 2);
    size_t index;
    List *list = m_lists[*++pos];
    if (indexValue->isString()) {
        const std::string &str = indexValue->toString();
        if (str == "last") {
            list->push_back(*READ_REG(0, 2));
            index = 0;
        } else if (str == "random") {
            size_t size = list->size();
            index = size == 0 ? 1 : randint<size_t>(1, size);
        } else
            index = 0;
    } else {
        index = indexValue->toLong();
        FIX_LIST_INDEX(index, list->size());
    }
    if ((index != 0) || list->empty()) {
        if (list->empty())
            list->push_back(*READ_REG(0, 2));
        else
            list->insert(index - 1, *READ_REG(0, 2));
    }
    FREE_REGS(2);
    DISPATCH();
}

do_list_replace : {
    const Value *indexValue = READ_REG(0, 2);
    size_t index;
    List *list = m_lists[*++pos];
    if (indexValue->isString()) {
        std::string str = indexValue->toString();
        if (str == "last")
            index = list->size();
        else if (str == "random") {
            size_t size = list->size();
            index = size == 0 ? 0 : randint<size_t>(1, size);
        } else
            index = 0;
    } else {
        index = indexValue->toLong();
        FIX_LIST_INDEX(index, list->size());
    }
    if (index != 0)
        list->operator[](index - 1) = *READ_REG(1, 2);
    FREE_REGS(2);
    DISPATCH();
}

do_list_get_item : {
    const Value *indexValue = READ_LAST_REG();
    size_t index;
    List *list = m_lists[*++pos];
    if (indexValue->isString()) {
        std::string str = indexValue->toString();
        if (str == "last")
            index = list->size();
        else if (str == "random") {
            size_t size = list->size();
            index = size == 0 ? 0 : randint<size_t>(1, size);
        } else
            index = 0;
    } else {
        index = indexValue->toLong();
        FIX_LIST_INDEX(index, list->size());
    }
    if (index == 0) {
        REPLACE_RET_VALUE("", 1);
    } else {
        REPLACE_RET_VALUE(list->operator[](index - 1), 1);
    }
    DISPATCH();
}

do_list_index_of:
    // TODO: Add size_t support to Value and remove the static_cast
    REPLACE_RET_VALUE(static_cast<long>(m_lists[*++pos]->indexOf(*READ_LAST_REG()) + 1), 1);
    DISPATCH();

do_list_length:
    // TODO: Add size_t support to Value and remove the static_cast
    ADD_RET_VALUE(static_cast<long>(m_lists[*++pos]->size()));
    DISPATCH();

do_list_contains:
    REPLACE_RET_VALUE(m_lists[*++pos]->contains(*READ_LAST_REG()), 1);
    DISPATCH();

do_str_concat:
    REPLACE_RET_VALUE(READ_REG(0, 2)->toString() + READ_REG(1, 2)->toString(), 2);
    FREE_REGS(1);
    DISPATCH();

do_str_at:
    REPLACE_RET_VALUE(READ_REG(0, 2)->toUtf16()[READ_REG(1, 2)->toLong()], 2);
    FREE_REGS(1);
    DISPATCH();

do_str_length:
    REPLACE_RET_VALUE(static_cast<long>(READ_REG(0, 1)->toUtf16().size()), 1);
    DISPATCH();

do_str_contains:
    REPLACE_RET_VALUE(READ_REG(0, 2)->toUtf16().find(READ_REG(1, 2)->toUtf16()) != std::u16string::npos, 2);
    FREE_REGS(1);
    DISPATCH();

do_exec:
    FREE_REGS(m_functions[*++pos](this));
    if (m_stop) {
        m_stop = false;
        if (m_regCount > 0) {
            std::cout << "warning: VM: " << m_regCount << " registers were leaked by the script; this is most likely a bug in the VM or in the compiler" << std::endl;
        }
        m_callTree.clear();
        m_procedureArgTree.clear();
        m_procedureArgs = nullptr;
        m_nextProcedureArgs = nullptr;
        if (!m_atomic)
            m_engine->breakFrame();
        return pos;
    }
    DISPATCH();

do_init_procedure:
    m_procedureArgTree.push_back({});
    if (m_procedureArgTree.size() >= 2)
        m_procedureArgs = &m_procedureArgTree[m_procedureArgTree.size() - 2];
    m_nextProcedureArgs = &m_procedureArgTree.back();
    DISPATCH();

do_call_procedure:
    m_callTree.push_back(++pos);
    m_procedureArgs = m_nextProcedureArgs;
    m_nextProcedureArgs = nullptr;
    pos = m_procedures[*pos];
    DISPATCH();

do_add_arg:
    m_nextProcedureArgs->push_back(*READ_LAST_REG());
    FREE_REGS(1);
    DISPATCH();

do_read_arg:
    ADD_RET_VALUE(m_procedureArgs->operator[](*++pos));
    DISPATCH();

do_break_atomic:
    m_atomic = false;
    DISPATCH();
}
