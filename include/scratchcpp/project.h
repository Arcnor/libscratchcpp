// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>
#include <functional>
#include "spimpl.h"

#include "global.h"

namespace libscratchcpp
{

class ProjectPrivate;

class IEngine;

/*!
 * \brief The Project class provides API for reading and running Scratch projects.
 *
 * \note Loading online projects is supported if the LIBSCRATCHCPP_NETWORK_SUPPORT option is set, just use setFileName("some URL")
 */
class LIBSCRATCHCPP_EXPORT Project
{
    public:
        Project();
        Project(const std::string &fileName);
        Project(const std::string &fileName, ScratchVersion scratchVersion);
        Project(const Project &) = delete;

        bool load();

        void start();
        void run();
        void runEventLoop();

        const std::string &fileName() const;
        void setFileName(const std::string &newFileName);

        ScratchVersion scratchVersion() const;
        void setScratchVersion(const ScratchVersion &version);

        std::shared_ptr<IEngine> engine() const;

        void setDownloadProgressCallback(const std::function<void(unsigned int, unsigned int)> &&f);

    private:
        spimpl::unique_impl_ptr<ProjectPrivate> impl;
};

} // namespace libscratchcpp
