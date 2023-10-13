/*
 * Copyright (C) 2022-2023 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "hostptr_copies_reader.h"

#include "generated_rpc_messages_level_zero.h"
#include "icd/icd_global_state.h"
#include "shared/ipc.h"
#include "shared/rpc.h"
#include "shared/shmem.h"

#include <cstring>

namespace Cal::Icd::LevelZero::Logic {

ze_result_t HostptrCopiesReader::readMemory(Cal::Rpc::ChannelClient &channel, Cal::Ipc::ShmemImporter &shmemImporter) {
    const auto transferDescs = getHostptrCopiesToUpdate(channel);
    if (transferDescs.empty()) {
        return ZE_RESULT_SUCCESS;
    }

    if (false == copyMappedMemory(shmemImporter, transferDescs)) {
        log<Verbosity::error>("Could not read required memory to user's stack/heap! Results of execution of command list would be invalid!");
        return ZE_RESULT_ERROR_OUT_OF_HOST_MEMORY;
    }

    return ZE_RESULT_SUCCESS;
}

std::vector<Cal::Rpc::ShmemTransferDesc> HostptrCopiesReader::getHostptrCopiesToUpdate(Cal::Rpc::ChannelClient &channel) {
    std::vector<Cal::Rpc::ShmemTransferDesc> memoryToRead{};

    while (true) {
        auto chunk = channel.acquireHostptrCopiesUpdate();
        if (chunk.bytesCountToCopy == 0u) {
            break;
        }

        memoryToRead.push_back(chunk);
    }

    return memoryToRead;
}

bool HostptrCopiesReader::copyMappedMemory(Cal::Ipc::ShmemImporter &shmemImporter,
                                           const std::vector<Cal::Rpc::ShmemTransferDesc> &transferDescs) {
    for (const auto &transfer : transferDescs) {
        auto shmem = shmemImporter.open(transfer.shmemId, transfer.underlyingSize, nullptr);
        if (!shmem.isValid()) {
            log<Verbosity::error>("Cannot map shared memory to perform transfer from service to client!");
            return false;
        }

        const auto sourceAddress = reinterpret_cast<uintptr_t>(shmem.getMmappedPtr()) + transfer.offsetFromMapping;
        const auto source = reinterpret_cast<const void *>(sourceAddress);
        const auto destination = reinterpret_cast<void *>(transfer.transferStart);

        std::memcpy(destination, source, transfer.bytesCountToCopy);
        shmemImporter.release(shmem);
    }

    return true;
}

} // namespace Cal::Icd::LevelZero::Logic
