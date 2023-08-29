// SPDX-License-Identifier: Apache-2.0

#pragma once

namespace libscratchcpp
{

struct CostumePrivate
{
        CostumePrivate();
        CostumePrivate(const CostumePrivate &) = delete;

        double bitmapResolution = 1;
        int rotationCenterX = 0;
        int rotationCenterY = 0;
};

} // namespace libscratchcpp
