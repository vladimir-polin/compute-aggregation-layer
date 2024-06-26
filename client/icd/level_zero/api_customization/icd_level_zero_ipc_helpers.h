/*
 * Copyright (C) 2022-2024 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once

#include "client/icd/icd_global_state.h"
#include "client/icd/level_zero/icd_level_zero.h"
#include "icd_level_zero_api.h"
#include "shared/control_messages.h"
#include "shared/log.h"

namespace Cal::Client::Icd::LevelZero::Ipc {

// to_local
template <typename IpcMemHandleT>
ze_result_t toLocalFds(const char *functionName, uint32_t numIpcHandles, IpcMemHandleT *pIpcHandles) {
    auto *globalL0Platform = Cal::Client::Icd::icdGlobalState.getL0Platform();
    auto &connection = globalL0Platform->getConnection();
    auto lock = connection.lock();

    static constexpr auto maxHandlesCount = Cal::Messages::ReqTransferFd::maxFdsCount;
    if (numIpcHandles > maxHandlesCount) {
        log<Verbosity::error>("%s: number of IPC handles to translate exceeds the maximum available count!", functionName, maxHandlesCount);
        return ZE_RESULT_ERROR_OUT_OF_DEVICE_MEMORY;
    }

    uint64_t knownFds = 0;
    static_assert(maxHandlesCount < 64);
    for (auto i = 0u; i < numIpcHandles; ++i) {
        int remoteFd = *(int *)pIpcHandles[i].data;
        auto localFd = globalL0Platform->translateRemoteFDToLocalFD(Cal::Utils::RemoteFd(remoteFd), true);
        if (localFd.valid()) {
            log<Verbosity::debug>("Reusing known IPC FD mapping remote:local = %d:%d", remoteFd, localFd.fd);
            std::memcpy(pIpcHandles[i].data, &localFd, sizeof(localFd));
            knownFds |= 1 << i;
        }
    }

    if (knownFds) {
        // some FDs are already known, translate only remaining ones
        for (auto i = 0u; i < numIpcHandles; ++i) {
            if (0 != (knownFds & (1 << i))) {
                continue;
            }
            auto ret = toLocalFds(functionName, 1, &pIpcHandles[i]);
            if (ZE_RESULT_SUCCESS != ret) {
                return ret;
            }
        }
        return ZE_RESULT_SUCCESS;
    }

    Cal::Messages::ReqTransferFd reqTransferFd{static_cast<uint16_t>(numIpcHandles)};
    for (auto i = 0u; i < numIpcHandles; ++i) {
        std::memcpy(&reqTransferFd.remoteFds[i], pIpcHandles[i].data, sizeof(reqTransferFd.remoteFds[i]));
    }

    if (false == connection.send(reqTransferFd)) {
        log<Verbosity::error>("%s: Could not request transfer of file descriptors!", functionName);
        return ZE_RESULT_ERROR_DEVICE_LOST;
    }

    std::array<int, maxHandlesCount> localFileDescriptors{};
    if (false == connection.receiveFds(localFileDescriptors.data(), numIpcHandles)) {
        log<Verbosity::error>("%s: Could not receive file descriptors from service!", functionName);
        return ZE_RESULT_ERROR_DEVICE_LOST;
    }

    for (auto i = 0u; i < numIpcHandles; ++i) {
        auto localFD = globalL0Platform->translateNewRemoteFDToLocalFD(Cal::Utils::RemoteFd(reqTransferFd.remoteFds[i]), Cal::Utils::LocalFd(localFileDescriptors[i]));
        std::memcpy(pIpcHandles[i].data, &localFD.fd, sizeof(localFD.fd));
    }

    return ZE_RESULT_SUCCESS;
}

// to_remote
template <typename IpcMemHandleT>
ze_result_t toRemoteFds(const char *functionName, uint32_t numIpcHandles, IpcMemHandleT *pIpcHandles) {
    auto *globalL0Platform = Cal::Client::Icd::icdGlobalState.getL0Platform();

    auto &connection = globalL0Platform->getConnection();
    auto lock = connection.lock();

    static constexpr auto maxHandlesCount = Cal::Messages::ReqReverseTransferFd::maxFdsCount;
    if (numIpcHandles > maxHandlesCount) {
        log<Verbosity::error>("%s: number of IPC handles to translate exceeds the maximum available count!", functionName, maxHandlesCount);
        return ZE_RESULT_ERROR_OUT_OF_DEVICE_MEMORY;
    }

    std::array<int, maxHandlesCount> localFileDescriptors{};
    for (auto i = 0u; i < numIpcHandles; ++i) {
        std::memcpy(&localFileDescriptors[i], pIpcHandles[i].data, sizeof(localFileDescriptors[i]));
    }

    uint64_t knownFds = 0;
    static_assert(maxHandlesCount < 64);
    for (auto i = 0u; i < numIpcHandles; ++i) {
        auto remoteFd = globalL0Platform->translateLocalFDToRemoteFD(Cal::Utils::LocalFd(localFileDescriptors[i]), Cal::Utils::RemoteFd::invalid());
        if (remoteFd.valid()) {
            log<Verbosity::debug>("Reusing known IPC FD mapping local:remote = %d:%d", localFileDescriptors[i], remoteFd.fd);
            std::memcpy(pIpcHandles[i].data, &remoteFd, sizeof(remoteFd));
            knownFds |= 1 << i;
        }
    }

    if (knownFds) {
        // some FDs are already known, translate only remaining ones
        for (auto i = 0u; i < numIpcHandles; ++i) {
            if (0 != (knownFds & (1 << i))) {
                continue;
            }
            auto ret = toRemoteFds(functionName, 1, &pIpcHandles[i]);
            if (ZE_RESULT_SUCCESS != ret) {
                return ret;
            }
        }
        return ZE_RESULT_SUCCESS;
    }

    Cal::Messages::ReqReverseTransferFd reqReverseTransferFd{static_cast<uint16_t>(numIpcHandles)};
    if (false == connection.send(reqReverseTransferFd)) {
        log<Verbosity::error>("%s: Could not request reverse transfer of file descriptor!", functionName);
        return ZE_RESULT_ERROR_DEVICE_LOST;
    }

    if (false == connection.sendFds(localFileDescriptors.data(), numIpcHandles)) {
        log<Verbosity::error>("%s: Could not send file descriptor to service for reverse transfer!", functionName);
        return ZE_RESULT_ERROR_DEVICE_LOST;
    }

    Cal::Messages::RespReverseTransferFd respReverseTransferFd{};
    if (false == connection.receive(respReverseTransferFd) || respReverseTransferFd.isInvalid()) {
        log<Verbosity::error>("%s: Could not receive remote file descriptor from service during reverse transfer!");
        return ZE_RESULT_ERROR_DEVICE_LOST;
    }

    for (auto i = 0u; i < numIpcHandles; ++i) {
        auto remoteFD = globalL0Platform->translateLocalFDToRemoteFD(Cal::Utils::LocalFd(localFileDescriptors[i]), Cal::Utils::RemoteFd(respReverseTransferFd.remoteFds[i]));
        std::memcpy(pIpcHandles[i].data, &remoteFD.fd, sizeof(remoteFD));
    }

    return ZE_RESULT_SUCCESS;
}

} // namespace Cal::Client::Icd::LevelZero::Ipc
