// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <scratchcpp/iblocksection.h>
#include <vector>

namespace libscratchcpp
{

class Target;
class Stage;
class Value;
class IGraphicsEffect;
class IRandomGenerator;

/*! \brief The LooksBlocks class contains the implementation of looks blocks. */
class LooksBlocks : public IBlockSection
{
    public:
        enum Inputs
        {
            CHANGE,
            SIZE,
            COSTUME,
            BACKDROP,
            VALUE,
            NUM
        };

        enum Fields
        {
            NUMBER_NAME,
            EFFECT,
            FRONT_BACK,
            FORWARD_BACKWARD
        };

        enum FieldValues
        {
            Number,
            Name,
            ColorEffect,
            FisheyeEffect,
            WhirlEffect,
            PixelateEffect,
            MosaicEffect,
            BrightnessEffect,
            GhostEffect,
            Front,
            Back,
            Forward,
            Backward
        };

        std::string name() const override;

        void registerBlocks(IEngine *engine) override;

        static void compileShow(Compiler *compiler);
        static void compileHide(Compiler *compiler);
        static void compileChangeEffectBy(Compiler *compiler);
        static void compileSetEffectTo(Compiler *compiler);
        static void compileClearGraphicEffects(Compiler *compiler);
        static void compileChangeSizeBy(Compiler *compiler);
        static void compileSetSizeTo(Compiler *compiler);
        static void compileSize(Compiler *compiler);
        static void compileSwitchCostumeTo(Compiler *compiler);
        static void compileNextCostume(Compiler *compiler);
        static void compileSwitchBackdropTo(Compiler *compiler);
        static void compileSwitchBackdropToAndWait(Compiler *compiler);
        static void compileNextBackdrop(Compiler *compiler);
        static void compileGoToFrontBack(Compiler *compiler);
        static void compileGoForwardBackwardLayers(Compiler *compiler);
        static void compileCostumeNumberName(Compiler *compiler);
        static void compileBackdropNumberName(Compiler *compiler);

        static unsigned int show(VirtualMachine *vm);
        static unsigned int hide(VirtualMachine *vm);

        static unsigned int changeEffectBy(VirtualMachine *vm);
        static unsigned int changeColorEffectBy(VirtualMachine *vm);
        static unsigned int changeFisheyeEffectBy(VirtualMachine *vm);
        static unsigned int changeWhirlEffectBy(VirtualMachine *vm);
        static unsigned int changePixelateEffectBy(VirtualMachine *vm);
        static unsigned int changeMosaicEffectBy(VirtualMachine *vm);
        static unsigned int changeBrightnessEffectBy(VirtualMachine *vm);
        static unsigned int changeGhostEffectBy(VirtualMachine *vm);

        static unsigned int setEffectTo(VirtualMachine *vm);
        static unsigned int setColorEffectTo(VirtualMachine *vm);
        static unsigned int setFisheyeEffectTo(VirtualMachine *vm);
        static unsigned int setWhirlEffectTo(VirtualMachine *vm);
        static unsigned int setPixelateEffectTo(VirtualMachine *vm);
        static unsigned int setMosaicEffectTo(VirtualMachine *vm);
        static unsigned int setBrightnessEffectTo(VirtualMachine *vm);
        static unsigned int setGhostEffectTo(VirtualMachine *vm);

        static unsigned int clearGraphicEffects(VirtualMachine *vm);
        static unsigned int changeSizeBy(VirtualMachine *vm);
        static unsigned int setSizeTo(VirtualMachine *vm);
        static unsigned int size(VirtualMachine *vm);

        static void setCostumeByIndex(Target *target, long index);
        static unsigned int switchCostumeToByIndex(VirtualMachine *vm);
        static unsigned int switchCostumeTo(VirtualMachine *vm);
        static unsigned int nextCostume(VirtualMachine *vm);
        static unsigned int previousCostume(VirtualMachine *vm);

        static void startBackdropScripts(VirtualMachine *vm, bool wait);
        static void switchBackdropToByIndexImpl(VirtualMachine *vm);
        static void switchBackdropToImpl(VirtualMachine *vm);
        static void nextBackdropImpl(VirtualMachine *vm);
        static void previousBackdropImpl(VirtualMachine *vm);
        static void randomBackdropImpl(VirtualMachine *vm);

        static unsigned int switchBackdropToByIndex(VirtualMachine *vm);
        static unsigned int switchBackdropTo(VirtualMachine *vm);
        static unsigned int switchBackdropToByIndexAndWait(VirtualMachine *vm);
        static unsigned int switchBackdropToAndWait(VirtualMachine *vm);
        static unsigned int nextBackdrop(VirtualMachine *vm);
        static unsigned int nextBackdropAndWait(VirtualMachine *vm);
        static unsigned int previousBackdrop(VirtualMachine *vm);
        static unsigned int previousBackdropAndWait(VirtualMachine *vm);
        static unsigned int randomBackdrop(VirtualMachine *vm);
        static unsigned int randomBackdropAndWait(VirtualMachine *vm);
        static unsigned int checkBackdropScripts(VirtualMachine *vm);

        static unsigned int goToFront(VirtualMachine *vm);
        static unsigned int goToBack(VirtualMachine *vm);

        static unsigned int goForwardLayers(VirtualMachine *vm);
        static unsigned int goBackwardLayers(VirtualMachine *vm);

        static unsigned int costumeNumber(VirtualMachine *vm);
        static unsigned int costumeName(VirtualMachine *vm);
        static unsigned int backdropNumber(VirtualMachine *vm);
        static unsigned int backdropName(VirtualMachine *vm);

        static inline std::vector<IGraphicsEffect *> m_customGraphicsEffects;
        static inline IGraphicsEffect *m_colorEffect = nullptr;
        static inline IGraphicsEffect *m_fisheyeEffect = nullptr;
        static inline IGraphicsEffect *m_whirlEffect = nullptr;
        static inline IGraphicsEffect *m_pixelateEffect = nullptr;
        static inline IGraphicsEffect *m_mosaicEffect = nullptr;
        static inline IGraphicsEffect *m_brightnessEffect = nullptr;
        static inline IGraphicsEffect *m_ghostEffect = nullptr;

        static IRandomGenerator *rng;
};

} // namespace libscratchcpp
