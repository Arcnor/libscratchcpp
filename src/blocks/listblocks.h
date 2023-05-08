// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../engine/iblocksection.h"

namespace libscratchcpp
{

/*! \brief The ListBlocks class contains the implementation of list blocks. */
class LIBSCRATCHCPP_EXPORT ListBlocks : public IBlockSection
{
    public:
        enum Inputs
        {
            ITEM,
            INDEX
        };

        enum Fields
        {
            LIST
        };

        ListBlocks();

        std::string name() const override;
        bool categoryVisible() const override;

        static void compileAddToList(Compiler *compiler);
        static void compileDeleteFromList(Compiler *compiler);
        static void compileDeleteAllOfList(Compiler *compiler);
        static void compileInsertToList(Compiler *compiler);
        static void compileReplaceItemOfList(Compiler *compiler);
        static void compileItemOfList(Compiler *compiler);
        static void compileItemNumberInList(Compiler *compiler);
        static void compileLengthOfList(Compiler *compiler);
        static void compileListContainsItem(Compiler *compiler);

    private:
        static int validateIndex(size_t index, size_t listLength);
};

} // namespace libscratchcpp
