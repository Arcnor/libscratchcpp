// SPDX-License-Identifier: Apache-2.0

#include <scratchcpp/iengine.h>
#include <scratchcpp/compiler.h>
#include <scratchcpp/sprite.h>

#include "looksblocks.h"

using namespace libscratchcpp;

std::string LooksBlocks::name() const
{
    return "Looks";
}

void LooksBlocks::registerBlocks(IEngine *engine)
{
    // Blocks
    engine->addCompileFunction(this, "looks_show", &compileShow);
    engine->addCompileFunction(this, "looks_hide", &compileHide);
    engine->addCompileFunction(this, "looks_changesizeby", &compileChangeSizeBy);
    engine->addCompileFunction(this, "looks_setsizeto", &compileSetSizeTo);

    // Inputs
    engine->addInput(this, "CHANGE", CHANGE);
    engine->addInput(this, "SIZE", SIZE);
}

void LooksBlocks::compileShow(Compiler *compiler)
{
    compiler->addFunctionCall(&show);
}

void LooksBlocks::compileHide(Compiler *compiler)
{
    compiler->addFunctionCall(&hide);
}

void LooksBlocks::compileChangeSizeBy(Compiler *compiler)
{
    compiler->addInput(CHANGE);
    compiler->addFunctionCall(&changeSizeBy);
}

void LooksBlocks::compileSetSizeTo(Compiler *compiler)
{
    compiler->addInput(SIZE);
    compiler->addFunctionCall(&setSizeTo);
}

unsigned int LooksBlocks::show(VirtualMachine *vm)
{
    Sprite *sprite = dynamic_cast<Sprite *>(vm->target());

    if (sprite)
        sprite->setVisible(true);

    return 0;
}

unsigned int LooksBlocks::hide(VirtualMachine *vm)
{
    Sprite *sprite = dynamic_cast<Sprite *>(vm->target());

    if (sprite)
        sprite->setVisible(false);

    return 0;
}

unsigned int LooksBlocks::changeSizeBy(VirtualMachine *vm)
{
    Sprite *sprite = dynamic_cast<Sprite *>(vm->target());

    if (sprite)
        sprite->setSize(sprite->size() + vm->getInput(0, 1)->toDouble());

    return 1;
}

unsigned int LooksBlocks::setSizeTo(VirtualMachine *vm)
{
    Sprite *sprite = dynamic_cast<Sprite *>(vm->target());

    if (sprite)
        sprite->setSize(vm->getInput(0, 1)->toDouble());

    return 1;
}
