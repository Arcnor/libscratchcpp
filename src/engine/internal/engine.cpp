// SPDX-License-Identifier: Apache-2.0

#include <scratchcpp/scratchconfiguration.h>
#include <scratchcpp/iblocksection.h>
#include <scratchcpp/script.h>
#include <scratchcpp/sprite.h>
#include <scratchcpp/stage.h>
#include <scratchcpp/broadcast.h>
#include <scratchcpp/compiler.h>
#include <scratchcpp/input.h>
#include <scratchcpp/inputvalue.h>
#include <scratchcpp/field.h>
#include <scratchcpp/block.h>
#include <scratchcpp/variable.h>
#include <scratchcpp/list.h>
#include <scratchcpp/comment.h>
#include <scratchcpp/costume.h>
#include <scratchcpp/keyevent.h>
#include <cassert>
#include <iostream>

#include "engine.h"
#include "blocksectioncontainer.h"
#include "timer.h"
#include "clock.h"
#include "blocks/standardblocks.h"
#include "blocks/eventblocks.h"

using namespace libscratchcpp;

const std::unordered_map<Engine::HatType, bool> Engine::m_hatRestartExistingThreads = {
    { HatType::GreenFlag, true },
    { HatType::BroadcastReceived, true },
    { HatType::BackdropChanged, true },
    { HatType::CloneInit, false },
    { HatType::KeyPressed, false }
};

Engine::Engine() :
    m_defaultTimer(std::make_unique<Timer>()),
    m_timer(m_defaultTimer.get()),
    m_clock(Clock::instance().get())
{
}

Engine::~Engine()
{
    m_clones.clear();
    m_executableTargets.clear();
}

void Engine::clear()
{
    m_sections.clear();
    m_targets.clear();
    m_broadcasts.clear();
    removeExecutableClones();
    m_clones.clear();

    m_running = false;
}

// Resolves ID references and sets pointers of entities.
void Engine::resolveIds()
{
    for (auto target : m_targets) {
        std::cout << "Processing target " << target->name() << "..." << std::endl;
        const auto &blocks = target->blocks();
        for (auto block : blocks) {
            auto container = blockSectionContainer(block->opcode());
            block->setNext(getBlock(block->nextId()));
            block->setParent(getBlock(block->parentId()));
            if (container)
                block->setCompileFunction(container->resolveBlockCompileFunc(block->opcode()));

            const auto &inputs = block->inputs();
            for (const auto &input : inputs) {
                input->setValueBlock(getBlock(input->valueBlockId()));
                if (container)
                    input->setInputId(container->resolveInput(input->name()));
                input->primaryValue()->setValuePtr(getEntity(input->primaryValue()->valueId()));
                input->secondaryValue()->setValuePtr(getEntity(input->primaryValue()->valueId()));
            }

            const auto &fields = block->fields();
            for (auto field : fields) {
                field->setValuePtr(getEntity(field->valueId()));
                if (container) {
                    field->setFieldId(container->resolveField(field->name()));
                    if (!field->valuePtr())
                        field->setSpecialValueId(container->resolveFieldValue(field->value().toString()));
                }
            }

            block->updateInputMap();
            block->updateFieldMap();

            auto comment = getComment(block->commentId());
            block->setComment(comment);

            if (comment) {
                comment->setBlock(block);
                assert(comment->blockId() == block->id());
            }
        }
    }
}

void Engine::compile()
{
    // Resolve entities by ID
    resolveIds();

    // Compile scripts to bytecode
    for (auto target : m_targets) {
        std::cout << "Compiling scripts in target " << target->name() << "..." << std::endl;
        std::unordered_map<std::string, unsigned int *> procedureBytecodeMap;
        Compiler compiler(this, target.get());
        const auto &blocks = target->blocks();
        for (auto block : blocks) {
            if (block->topLevel() && !block->shadow()) {
                auto section = blockSection(block->opcode());
                if (section) {
                    auto script = std::make_shared<Script>(target.get(), this);
                    m_scripts[block] = script;

                    compiler.compile(block);

                    script->setBytecode(compiler.bytecode());
                    if (block->opcode() == "procedures_definition") {
                        auto b = block->inputAt(block->findInput("custom_block"))->valueBlock();
                        procedureBytecodeMap[b->mutationPrototype()->procCode()] = script->bytecode();
                    }
                } else
                    std::cout << "warning: unsupported top level block: " << block->opcode() << std::endl;
            }
        }

        const std::vector<std::string> &procedures = compiler.procedures();
        std::vector<unsigned int *> procedureBytecodes;
        for (const std::string &code : procedures)
            procedureBytecodes.push_back(procedureBytecodeMap[code]);

        for (auto block : blocks) {
            if (m_scripts.count(block) == 1) {
                m_scripts[block]->setFunctions(m_functions);
                m_scripts[block]->setProcedures(procedureBytecodes);
                m_scripts[block]->setConstValues(compiler.constValues());
                m_scripts[block]->setVariables(compiler.variables());
                m_scripts[block]->setLists(compiler.lists());
            }
        }
    }
}

void Engine::start()
{
    // NOTE: Running scripts should be deleted, but this method will probably be removed anyway
    /*if (m_running)
        finalize();*/

    deleteClones();

    m_eventLoopMutex.lock();
    m_timer->reset();
    m_running = true;

    // Start "when green flag clicked" scripts
    startHats(HatType::GreenFlag, {}, nullptr);

    m_eventLoopMutex.unlock();
}

void Engine::stop()
{
    // https://github.com/scratchfoundation/scratch-vm/blob/f1aa92fad79af17d9dd1c41eeeadca099339a9f1/src/engine/runtime.js#L2057-L2081
    deleteClones();

    if (m_activeThread) {
        stopThread(m_activeThread.get());
        // NOTE: The project should continue running even after "stop all" is called and the remaining threads should be stepped once.
        // The remaining threads can even start new threads which will ignore the "stop all" call and will "restart" the project.
        // This is probably a bug in the Scratch VM, but let's keep it here to keep it compatible.
        m_threadsToStop = m_threads;
    } else {
        // If there isn't any active thread, it means the project was stopped from the outside
        // In this case all threads should be removed and the project should be considered stopped
        m_threads.clear();
        m_running = false;
    }
}

VirtualMachine *Engine::startScript(std::shared_ptr<Block> topLevelBlock, Target *target)
{
    return pushThread(topLevelBlock, target).get();
}

void Engine::broadcast(unsigned int index)
{
    if (index < 0 || index >= m_broadcasts.size())
        return;

    broadcastByPtr(m_broadcasts[index].get());
}

void Engine::broadcastByPtr(Broadcast *broadcast)
{
    startHats(HatType::BroadcastReceived, { { EventBlocks::Fields::BROADCAST_OPTION, broadcast->name() } }, nullptr);
}

void Engine::startBackdropScripts(Broadcast *broadcast)
{
    startHats(HatType::BackdropChanged, { { EventBlocks::Fields::BACKDROP, broadcast->name() } }, nullptr);
}

void Engine::stopScript(VirtualMachine *vm)
{
    stopThread(vm);
}

void Engine::stopTarget(Target *target, VirtualMachine *exceptScript)
{
    std::vector<VirtualMachine *> threads;

    for (auto thread : m_threads) {
        if ((thread->target() == target) && (thread.get() != exceptScript))
            threads.push_back(thread.get());
    }

    for (auto thread : threads)
        stopThread(thread);
}

void Engine::initClone(std::shared_ptr<Sprite> clone)
{
    if (!clone || ((m_cloneLimit >= 0) && (m_clones.size() >= m_cloneLimit)))
        return;

    Target *root = clone->cloneSprite();
    assert(root);

    if (!root)
        return;

#ifndef NDEBUG
    // Since we're initializing the clone, it shouldn't have any running scripts
    for (auto thread : m_threads)
        assert(thread->target() != clone.get() || thread->atEnd());
#endif

    startHats(HatType::CloneInit, {}, clone.get());

    assert(std::find(m_clones.begin(), m_clones.end(), clone) == m_clones.end());
    assert(std::find(m_executableTargets.begin(), m_executableTargets.end(), clone.get()) == m_executableTargets.end());
    m_clones.insert(clone);
    m_executableTargets.push_back(clone.get()); // execution order needs to be updated after this
}

void Engine::deinitClone(std::shared_ptr<Sprite> clone)
{
    m_clones.erase(clone);
    m_executableTargets.erase(std::remove(m_executableTargets.begin(), m_executableTargets.end(), clone.get()), m_executableTargets.end());
}

void Engine::run()
{
    start();
    eventLoop(true);
    finalize();
}

void Engine::runEventLoop()
{
    eventLoop();
}

void Engine::stopEventLoop()
{
    m_stopEventLoopMutex.lock();
    m_stopEventLoop = true;
    m_stopEventLoopMutex.unlock();
}

void Engine::setRedrawHandler(const std::function<void()> &handler)
{
    m_redrawHandler = handler;
}

void Engine::step()
{
    // https://github.com/scratchfoundation/scratch-vm/blob/f1aa92fad79af17d9dd1c41eeeadca099339a9f1/src/engine/runtime.js#L2087C6-L2155

    // Clean up threads that were told to stop during or since the last step
    m_threads.erase(std::remove_if(m_threads.begin(), m_threads.end(), [](std::shared_ptr<VirtualMachine> thread) { return thread->atEnd(); }), m_threads.end());

    m_redrawRequested = false;

    // Step threads
    stepThreads();

    // Render
    if (m_redrawHandler)
        m_redrawHandler();
}

std::vector<std::shared_ptr<VirtualMachine>> Engine::stepThreads()
{
    // https://github.com/scratchfoundation/scratch-vm/blob/develop/src/engine/sequencer.js#L70-L173
    const double WORK_TIME = 0.75 * m_frameDuration.count(); // 75% of frame duration
    assert(WORK_TIME > 0);
    auto stepStart = m_clock->currentSteadyTime();

    size_t numActiveThreads = 1; // greater than zero
    std::vector<std::shared_ptr<VirtualMachine>> doneThreads;

    auto elapsedTime = [this, &stepStart]() {
        std::chrono::steady_clock::time_point currentTime = m_clock->currentSteadyTime();
        std::chrono::milliseconds elapsedTime = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - stepStart);
        return elapsedTime.count();
    };

    while (!m_threads.empty() && numActiveThreads > 0 && elapsedTime() < WORK_TIME && (m_turboModeEnabled || !m_redrawRequested)) {
        numActiveThreads = 0;

        // Attempt to run each thread one time
        for (int i = 0; i < m_threads.size(); i++) {
            assert(i >= 0 && i < m_threads.size());
            m_activeThread = m_threads[i];

            // Check if the thread is done so it is not executed
            if (m_activeThread->atEnd()) {
                // Finished with this thread
                continue;
            }

            stepThread(m_activeThread);

            if (!m_activeThread->atEnd())
                numActiveThreads++;
        }

        // Remove threads in m_threadsToStop
        for (auto thread : m_threadsToStop)
            m_threads.erase(std::remove(m_threads.begin(), m_threads.end(), thread), m_threads.end());

        // Remove inactive threads (and add them to doneThreads)
        m_threads.erase(
            std::remove_if(
                m_threads.begin(),
                m_threads.end(),
                [&doneThreads](std::shared_ptr<VirtualMachine> thread) {
                    if (thread->atEnd()) {
                        doneThreads.push_back(thread);
                        return true;
                    } else
                        return false;
                }),
            m_threads.end());
    }

    if (m_threads.empty())
        m_running = false;

    m_activeThread = nullptr;
    return doneThreads;
}

void Engine::stepThread(std::shared_ptr<VirtualMachine> thread)
{
    // https://github.com/scratchfoundation/scratch-vm/blob/develop/src/engine/sequencer.js#L179-L276
    thread->run();
}

void Engine::eventLoop(bool untilProjectStops)
{
    updateFrameDuration();
    m_stopEventLoop = false;

    while (true) {
        auto tickStart = m_clock->currentSteadyTime();
        m_eventLoopMutex.lock();
        step();

        // Stop the event loop if the project has finished running (and untilProjectStops is set to true)
        if (untilProjectStops && m_threads.empty()) {
            m_eventLoopMutex.unlock();
            break;
        }

        // Stop the event loop if stopEventLoop() was called
        m_stopEventLoopMutex.lock();

        if (m_stopEventLoop) {
            m_stopEventLoopMutex.unlock();
            m_eventLoopMutex.unlock();
            break;
        }

        m_stopEventLoopMutex.unlock();

        std::chrono::steady_clock::time_point currentTime = m_clock->currentSteadyTime();
        std::chrono::milliseconds elapsedTime = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - tickStart);
        std::chrono::milliseconds sleepTime = m_frameDuration - elapsedTime;
        m_eventLoopMutex.unlock();

        // If there's any time left, sleep
        if (sleepTime > std::chrono::milliseconds::zero())
            m_clock->sleep(sleepTime);
    }

    finalize();
}

bool Engine::isRunning() const
{
    return m_running;
}

double Engine::fps() const
{
    return m_fps;
}

void Engine::setFps(double fps)
{
    m_fps = fps;
    updateFrameDuration();
}

bool Engine::turboModeEnabled() const
{
    return m_turboModeEnabled;
}

void Engine::setTurboModeEnabled(bool turboMode)
{
    m_turboModeEnabled = turboMode;
}

bool Engine::keyPressed(const std::string &name) const
{
    if (name == "any") {
        if (m_anyKeyPressed)
            return true;

        for (const auto &[key, value] : m_keyMap) {
            if (value)
                return true;
        }

        return false;
    }

    KeyEvent event(name);
    auto it = m_keyMap.find(event.name());

    if (it == m_keyMap.cend())
        return false;
    else
        return it->second;
}

void Engine::setKeyState(const std::string &name, bool pressed)
{
    KeyEvent event(name);
    setKeyState(event, pressed);
}

void Engine::setKeyState(const KeyEvent &event, bool pressed)
{
    m_keyMap[event.name()] = pressed;

    // Start "when key pressed" scripts
    if (pressed) {
        startHats(HatType::KeyPressed, { { EventBlocks::Fields::KEY_OPTION, event.name() } }, nullptr);
        startHats(HatType::KeyPressed, { { EventBlocks::Fields::KEY_OPTION, "any" } }, nullptr);
    }
}

void Engine::setAnyKeyPressed(bool pressed)
{
    m_anyKeyPressed = pressed;

    // Start "when key pressed" scripts
    if (pressed)
        startHats(HatType::KeyPressed, { { EventBlocks::Fields::KEY_OPTION, "any" } }, nullptr);
}

double Engine::mouseX() const
{
    return m_mouseX;
}

void Engine::setMouseX(double x)
{
    m_mouseX = x;
}

double Engine::mouseY() const
{
    return m_mouseY;
}

void Engine::setMouseY(double y)
{
    m_mouseY = y;
}

bool Engine::mousePressed() const
{
    return m_mousePressed;
}

void Engine::setMousePressed(bool pressed)
{
    m_mousePressed = pressed;
}

void Engine::clickTarget(Target *target)
{
    // TODO: Implement this (#92, #93)
}

unsigned int Engine::stageWidth() const
{
    return m_stageWidth;
}

void Engine::setStageWidth(unsigned int width)
{
    m_stageWidth = width;
}

unsigned int Engine::stageHeight() const
{
    return m_stageHeight;
}

void Engine::setStageHeight(unsigned int height)
{
    m_stageHeight = height;
}

int Engine::cloneLimit() const
{
    return m_cloneLimit;
}

void Engine::setCloneLimit(int limit)
{
    m_cloneLimit = limit < 0 ? -1 : limit;
}

int Engine::cloneCount() const
{
    return m_clones.size();
}

bool Engine::spriteFencingEnabled() const
{
    return m_spriteFencingEnabled;
}

void Engine::setSpriteFencingEnabled(bool enable)
{
    m_spriteFencingEnabled = enable;
}

bool Engine::broadcastRunning(unsigned int index)
{
    if (index < 0 || index >= m_broadcasts.size())
        return false;

    return broadcastByPtrRunning(m_broadcasts[index].get());
}

bool Engine::broadcastByPtrRunning(Broadcast *broadcast)
{
    if (broadcast->isBackdropBroadcast()) {
        // This broadcast belongs to a backdrop
        assert(m_broadcastMap.find(broadcast) == m_broadcastMap.cend());

        for (auto thread : m_threads) {
            if (!thread->atEnd()) {
                // TODO: Store the top block in Script
                Script *script = thread->script();
                auto it = std::find_if(m_scripts.begin(), m_scripts.end(), [script](const std::pair<std::shared_ptr<Block>, std::shared_ptr<Script>> pair) { return pair.second.get() == script; });
                assert(it != m_scripts.end());
                auto topBlock = it->first;

                const auto &scripts = m_backdropChangeHats[script->target()];
                auto scriptIt = std::find(scripts.begin(), scripts.end(), script);

                if ((scriptIt != scripts.end()) && (topBlock->findFieldById(EventBlocks::BACKDROP)->value().toString() == broadcast->name()))
                    return true;
            }
        }
    } else {
        // This is a regular broadcast
        assert(m_broadcastMap.find(broadcast) != m_broadcastMap.cend());
        const auto &scripts = m_broadcastMap[broadcast];

        for (auto thread : m_threads) {
            if (!thread->atEnd()) {
                auto it = std::find_if(scripts.begin(), scripts.end(), [thread](Script *script) { return thread->script() == script; });

                if (it != scripts.end())
                    return true;
            }
        }
    }
    return false;
}

void Engine::requestRedraw()
{
    m_redrawRequested = true;
}

ITimer *Engine::timer() const
{
    return m_timer;
}

void Engine::setTimer(ITimer *timer)
{
    m_timer = timer;
}

void Engine::registerSection(std::shared_ptr<IBlockSection> section)
{
    if (section) {
        if (m_sections.find(section) != m_sections.cend()) {
            std::cerr << "Warning: block section \"" << section->name() << "\" is already registered" << std::endl;
            return;
        }

        m_sections[section] = std::make_unique<BlockSectionContainer>();
        section->registerBlocks(this);
    }
}

std::vector<std::shared_ptr<IBlockSection>> Engine::registeredSections() const
{
    std::vector<std::shared_ptr<IBlockSection>> ret;

    for (const auto &[key, value] : m_sections)
        ret.push_back(key);

    return ret;
}

unsigned int Engine::functionIndex(BlockFunc f)
{
    auto it = std::find(m_functions.begin(), m_functions.end(), f);
    if (it != m_functions.end())
        return it - m_functions.begin();
    m_functions.push_back(f);
    return m_functions.size() - 1;
}

void Engine::addCompileFunction(IBlockSection *section, const std::string &opcode, BlockComp f)
{
    auto container = blockSectionContainer(section);

    if (container)
        container->addCompileFunction(opcode, f);
}

void Engine::addHatBlock(IBlockSection *section, const std::string &opcode)
{
    auto container = blockSectionContainer(section);

    if (container)
        container->addHatBlock(opcode);
}

void Engine::addInput(IBlockSection *section, const std::string &name, int id)
{
    auto container = blockSectionContainer(section);

    if (container)
        container->addInput(name, id);
}

void Engine::addField(IBlockSection *section, const std::string &name, int id)
{
    auto container = blockSectionContainer(section);

    if (container)
        container->addField(name, id);
}

void Engine::addFieldValue(IBlockSection *section, const std::string &value, int id)
{
    auto container = blockSectionContainer(section);

    if (container)
        container->addFieldValue(value, id);
}

const std::vector<std::shared_ptr<Broadcast>> &Engine::broadcasts() const
{
    return m_broadcasts;
}

void Engine::setBroadcasts(const std::vector<std::shared_ptr<Broadcast>> &broadcasts)
{
    m_broadcasts = broadcasts;
}

std::shared_ptr<Broadcast> Engine::broadcastAt(int index) const
{
    if (index < 0 || index >= m_broadcasts.size())
        return nullptr;

    return m_broadcasts[index];
}

int Engine::findBroadcast(const std::string &broadcastName) const
{
    int i = 0;
    for (auto broadcast : m_broadcasts) {
        if (broadcast->name() == broadcastName)
            return i;
        i++;
    }
    return -1;
}

int Engine::findBroadcastById(const std::string &broadcastId) const
{
    int i = 0;
    for (auto broadcast : m_broadcasts) {
        if (broadcast->id() == broadcastId)
            return i;
        i++;
    }
    return -1;
}

void Engine::addGreenFlagScript(std::shared_ptr<Block> hatBlock)
{
    addHatToMap(m_greenFlagHats, m_scripts[hatBlock].get());
}

void Engine::addBroadcastScript(std::shared_ptr<Block> whenReceivedBlock, Broadcast *broadcast)
{
    assert(!broadcast->isBackdropBroadcast());
    Script *script = m_scripts[whenReceivedBlock].get();
    auto it = m_broadcastMap.find(broadcast);

    if (it != m_broadcastMap.cend()) {
        auto &scripts = it->second;
        auto scriptIt = std::find(scripts.begin(), scripts.end(), script);

        if (scriptIt == scripts.end())
            scripts.push_back(script);
    } else
        m_broadcastMap[broadcast] = { script };

    addHatToMap(m_broadcastHats, script);
}

void Engine::addBackdropChangeScript(std::shared_ptr<Block> hatBlock)
{
    addHatToMap(m_backdropChangeHats, m_scripts[hatBlock].get());
}

void Engine::addCloneInitScript(std::shared_ptr<Block> hatBlock)
{
    addHatToMap(m_cloneInitHats, m_scripts[hatBlock].get());
}

void Engine::addKeyPressScript(std::shared_ptr<Block> hatBlock, std::string keyName)
{
    std::transform(keyName.begin(), keyName.end(), keyName.begin(), ::tolower);
    addHatToMap(m_whenKeyPressedHats, m_scripts[hatBlock].get());
}

const std::vector<std::shared_ptr<Target>> &Engine::targets() const
{
    return m_targets;
}

void Engine::setTargets(const std::vector<std::shared_ptr<Target>> &newTargets)
{
    m_targets = newTargets;
    m_executableTargets.clear();

    for (auto target : m_targets) {
        m_executableTargets.push_back(target.get());

        // Set engine in the target
        target->setEngine(this);
        auto blocks = target->blocks();

        for (auto block : blocks) {
            // Set engine and target in the block
            block->setEngine(this);
            block->setTarget(target.get());
        }
    }

    // Sort the executable targets by layer order
    std::sort(m_executableTargets.begin(), m_executableTargets.end(), [](Target *t1, Target *t2) { return t1->layerOrder() < t2->layerOrder(); });
}

Target *Engine::targetAt(int index) const
{
    if (index < 0 || index >= m_targets.size())
        return nullptr;

    return m_targets[index].get();
}

int Engine::findTarget(const std::string &targetName) const
{
    int i = 0;
    for (auto target : m_targets) {
        if ((target->isStage() && targetName == "_stage_") || (!target->isStage() && target->name() == targetName))
            return i;
        i++;
    }
    return -1;
}

void Engine::moveSpriteToFront(Sprite *sprite)
{
    if (!sprite || m_executableTargets.size() <= 2)
        return;

    auto it = std::find(m_executableTargets.begin(), m_executableTargets.end(), sprite);

    if (it != m_executableTargets.end()) {
        std::rotate(it, it + 1, m_executableTargets.end());
        updateSpriteLayerOrder();
    }
}

void Engine::moveSpriteToBack(Sprite *sprite)
{
    if (!sprite || m_executableTargets.size() <= 2)
        return;

    auto it = std::find(m_executableTargets.begin(), m_executableTargets.end(), sprite);

    if (it != m_executableTargets.end()) {
        std::rotate(m_executableTargets.begin() + 1, it, it + 1); // stage is always the first
        updateSpriteLayerOrder();
    }
}

void Engine::moveSpriteForwardLayers(Sprite *sprite, int layers)
{
    if (!sprite || layers == 0)
        return;

    auto it = std::find(m_executableTargets.begin(), m_executableTargets.end(), sprite);

    if (it == m_executableTargets.end())
        return;

    auto target = it + layers;

    if (target <= m_executableTargets.begin()) {
        moveSpriteToBack(sprite);
        return;
    }

    if (target >= m_executableTargets.end()) {
        moveSpriteToFront(sprite);
        return;
    }

    if (layers > 0)
        std::rotate(it, it + 1, target + 1);
    else
        std::rotate(target, it, it + 1);

    updateSpriteLayerOrder();
}

void Engine::moveSpriteBackwardLayers(Sprite *sprite, int layers)
{
    moveSpriteForwardLayers(sprite, -layers);
}

void Engine::moveSpriteBehindOther(Sprite *sprite, Sprite *other)
{
    if (sprite == other)
        return;

    auto itSprite = std::find(m_executableTargets.begin(), m_executableTargets.end(), sprite);
    auto itOther = std::find(m_executableTargets.begin(), m_executableTargets.end(), other);

    if ((itSprite == m_executableTargets.end()) || (itOther == m_executableTargets.end()))
        return;

    auto target = itOther - 1; // behind

    if (target < itSprite)
        target++;

    if (target <= m_executableTargets.begin()) {
        moveSpriteToBack(sprite);
        return;
    }

    if (target >= m_executableTargets.end()) {
        moveSpriteToFront(sprite);
        return;
    }

    if (target > itSprite)
        std::rotate(itSprite, itSprite + 1, target + 1);
    else
        std::rotate(target, itSprite, itSprite + 1);

    updateSpriteLayerOrder();
}

Stage *Engine::stage() const
{
    auto it = std::find_if(m_targets.begin(), m_targets.end(), [](std::shared_ptr<Target> target) { return target && target->isStage(); });

    if (it == m_targets.end())
        return nullptr;
    else
        return dynamic_cast<Stage *>((*it).get());
}

const std::vector<std::string> &Engine::extensions() const
{
    return m_extensions;
}

void Engine::setExtensions(const std::vector<std::string> &newExtensions)
{
    m_sections.clear();
    m_extensions = newExtensions;

    // Register standard block sections
    ScratchConfiguration::getExtension<StandardBlocks>()->registerSections(this);

    // Register block sections of extensions
    for (auto ext : m_extensions) {
        IExtension *ptr = ScratchConfiguration::getExtension(ext);
        if (ptr)
            ptr->registerSections(this);
        else
            std::cerr << "Unsupported extension: " << ext << std::endl;
    }
}

const std::unordered_map<std::shared_ptr<Block>, std::shared_ptr<Script>> &Engine::scripts() const
{
    return m_scripts;
}

// Returns the block with the given ID.
std::shared_ptr<Block> Engine::getBlock(const std::string &id)
{
    if (id.empty())
        return nullptr;

    for (auto target : m_targets) {
        int index = target->findBlock(id);
        if (index != -1)
            return target->blockAt(index);
    }

    return nullptr;
}

// Returns the variable with the given ID.
std::shared_ptr<Variable> Engine::getVariable(const std::string &id)
{
    if (id.empty())
        return nullptr;

    for (auto target : m_targets) {
        int index = target->findVariableById(id);
        if (index != -1)
            return target->variableAt(index);
    }

    return nullptr;
}

// Returns the Scratch list with the given ID.
std::shared_ptr<List> Engine::getList(const std::string &id)
{
    if (id.empty())
        return nullptr;

    for (auto target : m_targets) {
        int index = target->findListById(id);
        if (index != -1)
            return target->listAt(index);
    }

    return nullptr;
}

// Returns the broadcast with the given ID.
std::shared_ptr<Broadcast> Engine::getBroadcast(const std::string &id)
{
    if (id.empty())
        return nullptr;

    int index = findBroadcastById(id);
    if (index != -1)
        return broadcastAt(index);

    return nullptr;
}

// Returns the comment with the given ID.
std::shared_ptr<Comment> Engine::getComment(const std::string &id)
{
    if (id.empty())
        return nullptr;

    for (auto target : m_targets) {
        int index = target->findComment(id);
        if (index != -1)
            return target->commentAt(index);
    }

    return nullptr;
}

// Returns the entity with the given ID. \see IEntity
std::shared_ptr<Entity> Engine::getEntity(const std::string &id)
{
    // Blocks
    auto block = getBlock(id);
    if (block)
        return std::static_pointer_cast<Entity>(block);

    // Variables
    auto variable = getVariable(id);
    if (variable)
        return std::static_pointer_cast<Entity>(variable);

    // Lists
    auto list = getList(id);
    if (list)
        return std::static_pointer_cast<Entity>(list);

    // Broadcasts
    auto broadcast = getBroadcast(id);
    if (broadcast)
        return std::static_pointer_cast<Entity>(broadcast);

    return nullptr;
}

std::shared_ptr<IBlockSection> Engine::blockSection(const std::string &opcode) const
{
    for (const auto &pair : m_sections) {
        auto block = pair.second->resolveBlockCompileFunc(opcode);
        if (block)
            return pair.first;
    }

    return nullptr;
}

void Engine::addHatToMap(std::unordered_map<Target *, std::vector<Script *>> &map, Script *script)
{
    if (!script)
        return;

    assert(script->target());
    Target *target = script->target();
    auto it = map.find(target);

    if (it != map.cend()) {
        auto &scripts = it->second;
        auto scriptIt = std::find(scripts.begin(), scripts.end(), script);

        if (scriptIt == scripts.end())
            scripts.push_back(script);
    } else
        map[target] = { script };
}

const std::vector<Script *> &Engine::getHats(Target *target, HatType type)
{
    assert(target);

    // Get root if this is a clone
    if (!target->isStage()) {
        Sprite *sprite = dynamic_cast<Sprite *>(target);
        assert(sprite);

        if (sprite->isClone())
            target = sprite->cloneSprite();
    }

    switch (type) {
        case HatType::GreenFlag:
            return m_greenFlagHats[target];

        case HatType::BroadcastReceived:
            return m_broadcastHats[target];

        case HatType::BackdropChanged:
            return m_backdropChangeHats[target];

        case HatType::CloneInit:
            return m_cloneInitHats[target];

        case HatType::KeyPressed:
            return m_whenKeyPressedHats[target];

        default: {
            static const std::vector<Script *> empty = {};
            return empty;
        }
    }
}

void Engine::updateSpriteLayerOrder()
{
    assert(m_executableTargets.empty() || m_executableTargets[0]->isStage());

    for (size_t i = 1; i < m_executableTargets.size(); i++) // i = 1 to skip the stage
        m_executableTargets[i]->setLayerOrder(i);
}

BlockSectionContainer *Engine::blockSectionContainer(const std::string &opcode) const
{
    for (const auto &pair : m_sections) {
        auto block = pair.second->resolveBlockCompileFunc(opcode);
        if (block)
            return pair.second.get();
    }

    return nullptr;
}

BlockSectionContainer *Engine::blockSectionContainer(IBlockSection *section) const
{
    if (!section)
        return nullptr;

    for (const auto &pair : m_sections) {
        if (pair.first.get() == section)
            return pair.second.get();
    }

    return nullptr;
}

void Engine::finalize()
{
    m_eventLoopMutex.lock();
    m_threads.clear();
    m_running = false;
    m_redrawRequested = false;
    m_eventLoopMutex.unlock();
}

void Engine::deleteClones()
{
    m_eventLoopMutex.lock();
    removeExecutableClones();
    m_clones.clear();

    for (auto target : m_targets) {
        Sprite *sprite = dynamic_cast<Sprite *>(target.get());

        if (sprite) {
            std::vector<std::shared_ptr<Sprite>> clones = sprite->clones();

            for (auto clone : clones) {
                assert(clone);
                clone->deleteClone();
            }
        }
    }

    m_eventLoopMutex.unlock();
}

void Engine::removeExecutableClones()
{
    // Remove clones from the executable targets
    for (std::shared_ptr<Sprite> clone : m_clones)
        m_executableTargets.erase(std::remove(m_executableTargets.begin(), m_executableTargets.end(), clone.get()), m_executableTargets.end());
}

void Engine::updateFrameDuration()
{
    m_frameDuration = std::chrono::milliseconds(static_cast<long>(1000 / m_fps));
}

void Engine::addRunningScript(std::shared_ptr<VirtualMachine> vm)
{
    m_threads.push_back(vm);
}

std::shared_ptr<VirtualMachine> Engine::pushThread(std::shared_ptr<Block> block, Target *target)
{
    // https://github.com/scratchfoundation/scratch-vm/blob/f1aa92fad79af17d9dd1c41eeeadca099339a9f1/src/engine/runtime.js#L1649-L1661
    if (!block) {
        std::cerr << "error: tried to start a script with a null block" << std::endl;
        assert(false);
        return nullptr;
    }

    if (!target) {
        std::cerr << "error: scripts must be started by a target" << std::endl;
        assert(false);
        return nullptr;
    }

    auto script = m_scripts[block];
    std::shared_ptr<VirtualMachine> vm = script->start(target);
    addRunningScript(vm);
    return vm;
}

void Engine::stopThread(VirtualMachine *thread)
{
    // https://github.com/scratchfoundation/scratch-vm/blob/f1aa92fad79af17d9dd1c41eeeadca099339a9f1/src/engine/runtime.js#L1667-L1672
    assert(thread);
    thread->kill();
}

std::shared_ptr<VirtualMachine> Engine::restartThread(std::shared_ptr<VirtualMachine> thread)
{
    // https://github.com/scratchfoundation/scratch-vm/blob/f1aa92fad79af17d9dd1c41eeeadca099339a9f1/src/engine/runtime.js#L1681C30-L1694
    std::shared_ptr<VirtualMachine> newThread = thread->script()->start(thread->target());
    auto it = std::find(m_threads.begin(), m_threads.end(), thread);

    if (it != m_threads.end()) {
        auto i = it - m_threads.begin();
        m_threads[i] = newThread;
        return newThread;
    }

    addRunningScript(thread);
    return thread;
}

template<typename F>
void Engine::allScriptsByOpcodeDo(HatType hatType, F &&f, Target *optTarget)
{
    // https://github.com/scratchfoundation/scratch-vm/blob/f1aa92fad79af17d9dd1c41eeeadca099339a9f1/src/engine/runtime.js#L1797-L1809
    std::vector<Target *> *targetsPtr = &m_executableTargets;

    if (optTarget)
        targetsPtr = new std::vector<Target *>({ optTarget });

    const std::vector<Target *> targets = *targetsPtr;

    for (int t = targets.size() - 1; t >= 0; t--) {
        Target *target = targets[t];
        const auto &scripts = getHats(target, hatType);

        for (size_t j = 0; j < scripts.size(); j++)
            f(scripts[j], target);
    }

    if (optTarget)
        delete targetsPtr;
}

std::vector<std::shared_ptr<VirtualMachine>> Engine::startHats(HatType hatType, const std::unordered_map<int, std::string> &optMatchFields, Target *optTarget)
{
    // https://github.com/scratchfoundation/scratch-vm/blob/f1aa92fad79af17d9dd1c41eeeadca099339a9f1/src/engine/runtime.js#L1818-L1889
    std::vector<std::shared_ptr<VirtualMachine>> newThreads;

    allScriptsByOpcodeDo(
        hatType,
        [this, hatType, &optMatchFields, &newThreads](Script *script, Target *target) {
            // TODO: Store the top block in Script
            auto it = std::find_if(m_scripts.begin(), m_scripts.end(), [script](const std::pair<std::shared_ptr<Block>, std::shared_ptr<Script>> pair) { return pair.second.get() == script; });
            assert(it != m_scripts.end());
            auto topBlock = it->first;

            // Match any requested fields
            for (const auto &[fieldId, fieldValue] : optMatchFields) {
                assert(fieldId > -1);
                assert(topBlock->findFieldById(fieldId));

                if (topBlock->findFieldById(fieldId)->value().toString() != fieldValue) {
                    // Field mismatch
                    return;
                }
            }

            if (m_hatRestartExistingThreads.at(hatType)) {
                // Restart existing threads
                for (auto thread : m_threads) {
                    if (thread->target() == target && thread->script() == script) {
                        newThreads.push_back(restartThread(thread));
                        return;
                    }
                }
            } else {
                // Give up if any threads with the top block are running
                for (auto thread : m_threads) {
                    if (thread->target() == target && thread->script() == script && !thread->atEnd()) {
                        // Some thread is already running
                        return;
                    }
                }
            }

            // Start the thread with this top block
            newThreads.push_back(pushThread(topBlock, target));
        },
        optTarget);

    // Run edge-triggered hats (for compatibility with Scratch 2)
    // TODO: Find out what "edge-triggered" hats are and execute them
    // Uncommenting this would cause infinite recursion in some cases, so let's keep it commented
    /*for (auto thread : newThreads)
        thread->run();*/

    return newThreads;
}
