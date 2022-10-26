/*
 * Copyright (C) 2022 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "icd/icd_level_zero.h"

#include "generated_icd_level_zero.h"
#include "generated_rpc_messages_level_zero.h"
#include "icd/icd_global_state.h"
#include "include/cal.h"
#include "shared/log.h"
#include "shared/usm.h"

#include <algorithm>
#include <cstring>
#include <iterator>

namespace Cal {
namespace Icd {
namespace LevelZero {

ze_result_t zeInit(ze_init_flags_t flags) {
    const auto platform = Cal::Icd::icdGlobalState.getL0Platform();
    if (!platform || !platform->valid()) {
        log<Verbosity::error>("CAL service not available. zeInit() cannot be performed!");
        return ZE_RESULT_ERROR_UNINITIALIZED;
    }

    log<Verbosity::info>("Initializing LevelZero! Flags argument used with zeInit() will be ignored by Compute Aggregation Layer service.");
    return Cal::Icd::LevelZero::zeInitRpcHelper(flags);
}

ze_result_t zeDriverGet(uint32_t *pCount, ze_driver_handle_t *phDrivers) {
    const auto platform = Cal::Icd::icdGlobalState.getL0Platform();
    if (!platform || !platform->valid()) {
        log<Verbosity::debug>("CAL service not available. Trying to return zero available drivers!");
        if (pCount) {
            *pCount = 0;
        }
        return ZE_RESULT_SUCCESS;
    }

    if (nullptr == platform->asRemoteObject()) {
        log<Verbosity::debug>("Getting driver from deamon via zeDriverGet()");
        ze_driver_handle_t calDriverHandle{};

        uint32_t handleDriversArraySize{1};
        auto ret = Cal::Icd::LevelZero::zeDriverGetRpcHelper(&handleDriversArraySize, &calDriverHandle);
        if ((ZE_RESULT_SUCCESS != ret) || (nullptr == calDriverHandle)) {
            log<Verbosity::debug>("Failed to get ze_driver_handle_t from service");
            if (pCount) {
                *pCount = 0;
            }
            return ZE_RESULT_SUCCESS;
        }

        log<Verbosity::debug>("Got ze_driver_handle_t from service : %p", calDriverHandle);
        platform->setRemoteObject(calDriverHandle);
    }

    if (pCount) {
        if (*pCount > 0) {
            phDrivers[0] = platform;
        }

        *pCount = 1;
    }

    return ZE_RESULT_SUCCESS;
}
ze_result_t zeDeviceGet(ze_driver_handle_t hDriver, uint32_t *pCount, ze_device_handle_t *phDevices) {
    auto platform = Cal::Icd::icdGlobalState.getL0Platform();
    if (!platform || !platform->valid() || hDriver != static_cast<ze_driver_handle_t>(platform)) {
        return Cal::Icd::LevelZero::zeDeviceGetRpcHelper(hDriver, pCount, phDevices);
    }

    if (platform->isZeAffinityMaskPresent()) {
        auto &availableDevices = platform->getFilteredDevices();
        auto numAvailableDevices = static_cast<uint32_t>(availableDevices.size());
        if (phDevices == nullptr) {
            *pCount = numAvailableDevices;
        } else {
            auto numDevices = std::min(*pCount, numAvailableDevices);
            memcpy(phDevices, availableDevices.data(), numDevices * sizeof(ze_device_handle_t));
        }
        return ZE_RESULT_SUCCESS;
    }
    return Cal::Icd::LevelZero::zeDeviceGetRpcHelper(hDriver, pCount, phDevices);
}

ze_result_t zeDeviceGetSubDevices(ze_device_handle_t hDevice, uint32_t *pCount, ze_device_handle_t *phDevices) {
    auto device = static_cast<IcdL0Device *>(hDevice);
    if (!device) {
        return Cal::Icd::LevelZero::zeDeviceGetSubDevicesRpcHelper(hDevice, pCount, phDevices);
    }
    device->ensureIsLocalObject();

    if (device->isZeAffinityMaskPresent()) {
        auto &availableDevices = device->getFilteredDevices();
        auto numAvailableDevices = static_cast<uint32_t>(availableDevices.size());
        if (numAvailableDevices == 1u) {
            numAvailableDevices = 0u;
        }
        if (phDevices == nullptr) {
            *pCount = numAvailableDevices;
        } else {
            auto numDevices = std::min(*pCount, numAvailableDevices);
            memcpy(phDevices, availableDevices.data(), numDevices * sizeof(ze_device_handle_t));
        }
        return ZE_RESULT_SUCCESS;
    }
    return Cal::Icd::LevelZero::zeDeviceGetSubDevicesRpcHelper(hDevice, pCount, phDevices);
}

ze_result_t zeMemAllocHost(ze_context_handle_t hContext, const ze_host_mem_alloc_desc_t *host_desc, size_t size, size_t alignment, void **pptr) {
    Cal::Rpc::LevelZero::ZeMemAllocHostRpcM::ImplicitArgs implicitArgs;
    auto result = Cal::Icd::LevelZero::zeMemAllocHostRpcHelper(hContext, host_desc, size, alignment, pptr, implicitArgs);
    if (result != ZE_RESULT_SUCCESS) {
        return result;
    }
    if (false == Cal::Icd::icdGlobalState.getL0Platform()->openNewUsmHostOrSharedPointer(hContext, *pptr, implicitArgs.shmem_resource, implicitArgs.offset_within_resource, implicitArgs.aligned_size)) {
        log<Verbosity::error>("Failed to open USM shared/host shmem");
        Cal::Icd::LevelZero::zeMemFree(hContext, *pptr);
        *pptr = nullptr;
    }
    return result;
}

ze_result_t zeMemAllocShared(ze_context_handle_t hContext, const ze_device_mem_alloc_desc_t *device_desc, const ze_host_mem_alloc_desc_t *host_desc, size_t size, size_t alignment, ze_device_handle_t hDevice, void **pptr) {
    Cal::Rpc::LevelZero::ZeMemAllocSharedRpcM::ImplicitArgs implicitArgs;
    auto result = Cal::Icd::LevelZero::zeMemAllocSharedRpcHelper(hContext, device_desc, host_desc, size, alignment, hDevice, pptr, implicitArgs);
    if (result != ZE_RESULT_SUCCESS) {
        return result;
    }
    if (false == Cal::Icd::icdGlobalState.getL0Platform()->openNewUsmHostOrSharedPointer(hContext, *pptr, implicitArgs.shmem_resource, implicitArgs.offset_within_resource, implicitArgs.aligned_size)) {
        log<Verbosity::error>("Failed to open USM shared/host shmem");
        Cal::Icd::LevelZero::zeMemFree(hContext, *pptr);
        *pptr = nullptr;
    }
    return result;
}

ze_result_t zeMemGetAllocProperties(ze_context_handle_t hContext, const void *ptr, ze_memory_allocation_properties_t *pMemAllocProperties, ze_device_handle_t *phDevice) {
    auto l0Context = static_cast<IcdL0Context *>(hContext);
    auto found = l0Context->allocPropertiesCache.obtainProperties(ptr, pMemAllocProperties, phDevice);
    if (found) {
        return ZE_RESULT_SUCCESS;
    }
    ze_device_handle_t localDeviceHandle;
    auto ret = zeMemGetAllocPropertiesRpcHelper(hContext, ptr, pMemAllocProperties, &localDeviceHandle);
    l0Context->allocPropertiesCache.cacheProperties(ptr, pMemAllocProperties, &localDeviceHandle);
    if (phDevice) {
        phDevice[0] = localDeviceHandle;
    }
    return ret;
}

ze_result_t zeCommandListAppendMemoryFill(ze_command_list_handle_t hCommandList,
                                          void *ptr,
                                          const void *pattern,
                                          size_t pattern_size,
                                          size_t size,
                                          ze_event_handle_t hSignalEvent,
                                          uint32_t numWaitEvents,
                                          ze_event_handle_t *phWaitEvents) {
    auto icdCommandList = static_cast<IcdL0CommandList *>(hCommandList);
    if (icdCommandList == nullptr) {
        return ZE_RESULT_ERROR_INVALID_NULL_HANDLE;
    }

    if (ptr == nullptr || pattern == nullptr) {
        return ZE_RESULT_ERROR_INVALID_NULL_POINTER;
    }

    auto globalL0Platform = Cal::Icd::icdGlobalState.getL0Platform();

    const void *ptrs[] = {ptr, pattern};
    bool testResults[2] = {};
    globalL0Platform->areUsm(2, ptrs, testResults);

    const auto &[dstIsUsm, srcIsUsm] = testResults;
    if (dstIsUsm && srcIsUsm) {
        return zeCommandListAppendMemoryFillRpcHelperUsm2Usm(hCommandList, ptr, pattern, pattern_size, size, hSignalEvent, numWaitEvents, phWaitEvents);
    } else if (dstIsUsm) {
        return zeCommandListAppendMemoryFillRpcHelperMalloc2Usm(hCommandList, ptr, pattern, pattern_size, size, hSignalEvent, numWaitEvents, phWaitEvents);
    } else if (srcIsUsm) {
        if (icdCommandList->isImmediate()) {
            log<Verbosity::error>("zeCommandListAppendMemoryFill() from USM to host's heap/stack is not supported yet for immediate command lists!");
        } else {
            icdCommandList->registerMemoryToRead(ptr, size);
            return zeCommandListAppendMemoryFillRpcHelperUsm2Malloc(hCommandList, ptr, pattern, pattern_size, size, hSignalEvent, numWaitEvents, phWaitEvents);
        }
    } else {
        if (IcdL0CommandList::rangesOverlap(pattern, pattern_size, ptr, size)) {
            log<Verbosity::debug>("zeCommandListAppendMemoryFill(): host's heap/stack memory blocks overlap!");
            return ZE_RESULT_ERROR_OVERLAPPING_REGIONS;
        }

        if (icdCommandList->isImmediate()) {
            log<Verbosity::debug>("zeCommandListAppendMemoryFill() from host's heap/stack to host's heap/stack is not supported yet for immediate command lists!");
        } else {
            icdCommandList->registerMemoryToRead(ptr, size);
            return zeCommandListAppendMemoryFillRpcHelperMalloc2Malloc(hCommandList, ptr, pattern, pattern_size, size, hSignalEvent, numWaitEvents, phWaitEvents);
        }
    }

    return ZE_RESULT_ERROR_UNSUPPORTED_FEATURE;
}

ze_result_t zeKernelSetArgumentValue(ze_kernel_handle_t hKernel, uint32_t argIndex, size_t argSize, const void *pArgValue) {
    auto cacheEnabled = Cal::Icd::icdGlobalState.isCacheEnabled();
    if (!cacheEnabled) {
        return zeKernelSetArgumentValueRpcHelper(hKernel, argIndex, argSize, pArgValue);
    }

    auto l0Kernel = static_cast<IcdL0Kernel *>(hKernel);

    auto cacheRet = l0Kernel->zeKernelSetArgumentValueCache.findCachedKernelArg(argIndex, argSize, pArgValue);
    if (cacheRet != l0Kernel->zeKernelSetArgumentValueCache.cache.end()) {
        return ZE_RESULT_SUCCESS;
    }

    l0Kernel->zeKernelSetArgumentValueCache.cacheKernelArg(argIndex, argSize, pArgValue);
    return zeKernelSetArgumentValueRpcHelper(hKernel, argIndex, argSize, pArgValue);
}

ze_result_t zeDriverGetProperties(ze_driver_handle_t hDriver, ze_driver_properties_t *pDriverProperties) {
    return PropertiesCache::obtainProperties(static_cast<IcdL0Platform *>(hDriver), pDriverProperties, zeDriverGetPropertiesRpcHelper);
}
ze_result_t zeDriverGetIpcProperties(ze_driver_handle_t hDriver, ze_driver_ipc_properties_t *pIpcProperties) {
    return PropertiesCache::obtainProperties(static_cast<IcdL0Platform *>(hDriver), pIpcProperties, zeDriverGetIpcPropertiesRpcHelper);
}
ze_result_t zeDriverGetExtensionProperties(ze_driver_handle_t hDriver, uint32_t *pCount, ze_driver_extension_properties_t *pExtensionProperties) {
    return PropertiesCache::obtainProperties(static_cast<IcdL0Platform *>(hDriver), pCount, pExtensionProperties, zeDriverGetExtensionPropertiesRpcHelper);
}
ze_result_t zeDeviceGetProperties(ze_device_handle_t hDevice, ze_device_properties_t *pDeviceProperties) {
    return PropertiesCache::obtainProperties(static_cast<IcdL0Device *>(hDevice), pDeviceProperties, zeDeviceGetPropertiesRpcHelper);
}
ze_result_t zeDeviceGetComputeProperties(ze_device_handle_t hDevice, ze_device_compute_properties_t *pComputeProperties) {
    return PropertiesCache::obtainProperties(static_cast<IcdL0Device *>(hDevice), pComputeProperties, zeDeviceGetComputePropertiesRpcHelper);
}
ze_result_t zeDeviceGetModuleProperties(ze_device_handle_t hDevice, ze_device_module_properties_t *pModuleProperties) {
    return PropertiesCache::obtainProperties(static_cast<IcdL0Device *>(hDevice), pModuleProperties, zeDeviceGetModulePropertiesRpcHelper);
}
ze_result_t zeDeviceGetMemoryAccessProperties(ze_device_handle_t hDevice, ze_device_memory_access_properties_t *pMemAccessProperties) {
    return PropertiesCache::obtainProperties(static_cast<IcdL0Device *>(hDevice), pMemAccessProperties, zeDeviceGetMemoryAccessPropertiesRpcHelper);
}
ze_result_t zeDeviceGetImageProperties(ze_device_handle_t hDevice, ze_device_image_properties_t *pImageProperties) {
    return PropertiesCache::obtainProperties(static_cast<IcdL0Device *>(hDevice), pImageProperties, zeDeviceGetImagePropertiesRpcHelper);
}
ze_result_t zeDeviceGetExternalMemoryProperties(ze_device_handle_t hDevice, ze_device_external_memory_properties_t *pExternalMemoryProperties) {
    return PropertiesCache::obtainProperties(static_cast<IcdL0Device *>(hDevice), pExternalMemoryProperties, zeDeviceGetExternalMemoryPropertiesRpcHelper);
}
ze_result_t zeDeviceGetCacheProperties(ze_device_handle_t hDevice, uint32_t *pCount, ze_device_cache_properties_t *pCacheProperties) {
    return PropertiesCache::obtainProperties(static_cast<IcdL0Device *>(hDevice), pCount, pCacheProperties, zeDeviceGetCachePropertiesRpcHelper);
}
ze_result_t zeDeviceGetCommandQueueGroupProperties(ze_device_handle_t hDevice, uint32_t *pCount, ze_command_queue_group_properties_t *pCommandQueueGroupProperties) {
    return PropertiesCache::obtainProperties(static_cast<IcdL0Device *>(hDevice), pCount, pCommandQueueGroupProperties, zeDeviceGetCommandQueueGroupPropertiesRpcHelper);
}
ze_result_t zeDeviceGetMemoryProperties(ze_device_handle_t hDevice, uint32_t *pCount, ze_device_memory_properties_t *pMemProperties) {
    return PropertiesCache::obtainProperties(static_cast<IcdL0Device *>(hDevice), pCount, pMemProperties, zeDeviceGetMemoryPropertiesRpcHelper);
}
ze_result_t zeModuleGetProperties(ze_module_handle_t hModule, ze_module_properties_t *pModuleProperties) {
    return PropertiesCache::obtainProperties(static_cast<IcdL0Module *>(hModule), pModuleProperties, zeModuleGetPropertiesRpcHelper);
}
ze_result_t zeKernelGetProperties(ze_kernel_handle_t hKernel, ze_kernel_properties_t *pKernelProperties) {
    return PropertiesCache::obtainProperties(static_cast<IcdL0Kernel *>(hKernel), pKernelProperties, zeKernelGetPropertiesRpcHelper);
}

ze_result_t zeCommandListAppendMemoryCopy(ze_command_list_handle_t hCommandList,
                                          void *dstptr,
                                          const void *srcptr,
                                          size_t size,
                                          ze_event_handle_t hSignalEvent,
                                          uint32_t numWaitEvents,
                                          ze_event_handle_t *phWaitEvents) {

    auto icdCommandList = static_cast<IcdL0CommandList *>(hCommandList);
    if (icdCommandList == nullptr) {
        return ZE_RESULT_ERROR_INVALID_NULL_HANDLE;
    }

    if (dstptr == nullptr || srcptr == nullptr) {
        return ZE_RESULT_ERROR_INVALID_NULL_POINTER;
    }

    auto globalL0Platform = Cal::Icd::icdGlobalState.getL0Platform();

    const void *ptrs[] = {dstptr, srcptr};
    bool testResults[2] = {};
    globalL0Platform->areUsm(2, ptrs, testResults);

    const auto &[dstIsUsm, srcIsUsm] = testResults;
    if (dstIsUsm && srcIsUsm) {
        return zeCommandListAppendMemoryCopyRpcHelperUsm2Usm(hCommandList, dstptr, srcptr, size, hSignalEvent, numWaitEvents, phWaitEvents);
    } else if (dstIsUsm) {
        if (icdCommandList->isImmediate()) {
            return zeCommandListAppendMemoryCopyRpcHelperMalloc2UsmImmediate(hCommandList, dstptr, srcptr, size, hSignalEvent, numWaitEvents, phWaitEvents);
        } else {
            icdCommandList->registerMemoryToWrite(srcptr, size);
            return zeCommandListAppendMemoryCopyRpcHelperMalloc2Usm(hCommandList, dstptr, srcptr, size, hSignalEvent, numWaitEvents, phWaitEvents);
        }
    } else if (srcIsUsm) {
        if (icdCommandList->isImmediate()) {
            log<Verbosity::error>("zeCommandListAppendMemoryCopy for USM2M is not supported for immediate command lists yet!");
        } else {
            icdCommandList->registerMemoryToRead(dstptr, size);
            return zeCommandListAppendMemoryCopyRpcHelperUsm2Malloc(hCommandList, dstptr, srcptr, size, hSignalEvent, numWaitEvents, phWaitEvents);
        }
    } else {
        if (IcdL0CommandList::rangesOverlap(srcptr, dstptr, size)) {
            log<Verbosity::debug>("zeCommandListAppendMemoryCopy(): host's heap/stack memory blocks overlap!");
            return ZE_RESULT_ERROR_OVERLAPPING_REGIONS;
        }

        if (icdCommandList->isImmediate()) {
            log<Verbosity::error>("zeCommandListAppendMemoryCopy for M2M is not supported for immediate command lists yet!");
        } else {
            icdCommandList->registerMemoryToWrite(srcptr, size);
            icdCommandList->registerMemoryToRead(dstptr, size);

            return zeCommandListAppendMemoryCopyRpcHelperMalloc2Malloc(hCommandList, dstptr, srcptr, size, hSignalEvent, numWaitEvents, phWaitEvents);
        }
    }

    return ZE_RESULT_ERROR_UNSUPPORTED_FEATURE;
}

void IcdL0CommandList::registerMemoryToWrite(const void *srcPtr, size_t srcSize) {
    std::lock_guard lock{memoryToWriteMutex};
    registerMemoryToContainer(srcPtr, srcSize, memoryToWrite);
}

void IcdL0CommandList::registerMemoryToRead(const void *dstPtr, size_t dstSize) {
    std::lock_guard lock{memoryToReadMutex};
    registerMemoryToContainer(dstPtr, dstSize, memoryToRead);
}

void IcdL0CommandList::registerMemoryToContainer(const void *ptr, size_t size, std::vector<ChunkEntry> &memory) {
    const auto overlaps = [ptr, size](const auto &chunk) {
        return IcdL0CommandList::rangesOverlap(chunk.address, chunk.size, ptr, size);
    };

    const auto overlappingCount = std::count_if(std::begin(memory), std::end(memory), overlaps);
    if (overlappingCount == 0) {
        memory.emplace_back(ptr, size);
        return;
    }

    if (overlappingCount == 1) {
        auto overlappingChunkIt = std::find_if(std::begin(memory), std::end(memory), overlaps);
        *overlappingChunkIt = mergeChunks(ChunkEntry{ptr, size}, *overlappingChunkIt);

        return;
    }

    std::vector<ChunkEntry> newChunks{};
    newChunks.reserve(memory.size());

    ChunkEntry currentChunk{ptr, size};
    for (auto &chunk : memory) {
        if (!rangesOverlap(chunk.address, chunk.size, ptr, size)) {
            newChunks.push_back(chunk);
            continue;
        }

        currentChunk = mergeChunks(currentChunk, chunk);
    }

    newChunks.push_back(currentChunk);
    memory = std::move(newChunks);
}

bool IcdL0CommandList::rangesOverlap(const void *srcPtr, const void *dstPtr, size_t size) {
    return rangesOverlap(srcPtr, size, dstPtr, size);
}

bool IcdL0CommandList::rangesOverlap(const void *srcPtr, size_t srcSize, const void *dstPtr, size_t dstSize) {
    const auto srcBegin = reinterpret_cast<uintptr_t>(srcPtr);
    const auto srcEnd = srcBegin + srcSize;

    const auto dstBegin = reinterpret_cast<uintptr_t>(dstPtr);
    const auto dstEnd = dstBegin + dstSize;

    return (srcBegin <= dstBegin && dstBegin < srcEnd) || (dstBegin <= srcBegin && srcBegin < dstEnd);
}

auto IcdL0CommandList::mergeChunks(const ChunkEntry &first, const ChunkEntry &second) -> ChunkEntry {
    const auto firstBegin = reinterpret_cast<uintptr_t>(first.address);
    const auto firstEnd = firstBegin + first.size;

    const auto secondBegin = reinterpret_cast<uintptr_t>(second.address);
    const auto secondEnd = secondBegin + second.size;

    const auto mergedBegin = std::min(firstBegin, secondBegin);
    const auto mergedEnd = std::max(firstEnd, secondEnd);

    const auto mergedAddress = reinterpret_cast<const void *>(mergedBegin);
    const auto mergedSize = static_cast<size_t>(mergedEnd - mergedBegin);

    return ChunkEntry{mergedAddress, mergedSize};
}

ze_result_t IcdL0CommandList::writeRequiredMemory() {
    std::lock_guard lock{memoryToWriteMutex};

    if (memoryToWrite.empty()) {
        return ZE_RESULT_SUCCESS;
    }

    uint32_t transferDescsCount{0};
    const auto queryCountResult = zeCommandQueueExecuteCommandListsCopyMemoryRpcHelper(static_cast<uint32_t>(memoryToWrite.size()),
                                                                                       memoryToWrite.data(),
                                                                                       &transferDescsCount,
                                                                                       nullptr);
    if (queryCountResult != ZE_RESULT_SUCCESS) {
        log<Verbosity::error>("Could not get total count of memory blocks to write from service! Execution of command list would be invalid!");
        return ZE_RESULT_ERROR_OUT_OF_HOST_MEMORY;
    }

    std::vector<Cal::Rpc::ShmemTransferDesc> transferDescs;
    transferDescs.resize(transferDescsCount);

    const auto queryTransferDescs = zeCommandQueueExecuteCommandListsCopyMemoryRpcHelper(static_cast<uint32_t>(memoryToWrite.size()),
                                                                                         memoryToWrite.data(),
                                                                                         &transferDescsCount,
                                                                                         transferDescs.data());
    if (queryTransferDescs != ZE_RESULT_SUCCESS) {
        log<Verbosity::error>("Could not get memory blocks to write from service! Execution of command list would be invalid!");
        return ZE_RESULT_ERROR_OUT_OF_HOST_MEMORY;
    }

    auto globalL0Platform = Cal::Icd::icdGlobalState.getL0Platform();
    if (!globalL0Platform->writeRequiredMemory(transferDescs)) {
        log<Verbosity::error>("Could not write required memory from user's stack/heap! Execution of command list would be invalid!");
        return ZE_RESULT_ERROR_OUT_OF_HOST_MEMORY;
    }

    return ZE_RESULT_SUCCESS;
}

ze_result_t IcdL0CommandList::readRequiredMemory() {
    std::lock_guard lock{memoryToReadMutex};

    if (memoryToRead.empty()) {
        return ZE_RESULT_SUCCESS;
    }

    uint32_t transferDescsCount{0};
    const auto queryCountResult = zeCommandQueueExecuteCommandListsCopyMemoryRpcHelper(static_cast<uint32_t>(memoryToRead.size()),
                                                                                       memoryToRead.data(),
                                                                                       &transferDescsCount,
                                                                                       nullptr);
    if (queryCountResult != ZE_RESULT_SUCCESS) {
        log<Verbosity::error>("Could not get total count of memory blocks to read from service! Execution of command list would be invalid!");
        return ZE_RESULT_ERROR_OUT_OF_HOST_MEMORY;
    }

    std::vector<Cal::Rpc::ShmemTransferDesc> transferDescs;
    transferDescs.resize(transferDescsCount);

    const auto queryTransferDescs = zeCommandQueueExecuteCommandListsCopyMemoryRpcHelper(static_cast<uint32_t>(memoryToRead.size()),
                                                                                         memoryToRead.data(),
                                                                                         &transferDescsCount,
                                                                                         transferDescs.data());
    if (queryTransferDescs != ZE_RESULT_SUCCESS) {
        log<Verbosity::error>("Could not get memory blocks to read from service! Execution of command list would be invalid!");
        return ZE_RESULT_ERROR_OUT_OF_HOST_MEMORY;
    }

    auto globalL0Platform = Cal::Icd::icdGlobalState.getL0Platform();
    if (!globalL0Platform->readRequiredMemory(transferDescs)) {
        log<Verbosity::error>("Could not read required memory to user's stack/heap! Results of execution of command list would be invalid!");
        return ZE_RESULT_ERROR_OUT_OF_HOST_MEMORY;
    }

    return ZE_RESULT_SUCCESS;
}

ze_result_t zeEventHostSynchronize(ze_event_handle_t hEvent, uint64_t timeout) {
    log<Verbosity::debug>("Detected call to zeEventHostSynchronize()! "
                          "If events were used to synchronize zeCommandListAppendMemoryCopy() to user's heap/stack, then operation result may be invalid!");
    return zeEventHostSynchronizeRpcHelper(hEvent, timeout);
}

ze_result_t zeFenceHostSynchronize(ze_fence_handle_t hFence, uint64_t timeout) {
    auto icdFence = static_cast<IcdL0Fence *>(hFence);
    auto icdQueue = icdFence->peekQueue();
    auto queueLock = icdQueue->lock();

    const auto result = zeFenceHostSynchronizeRpcHelper(hFence, timeout);
    if (result == ZE_RESULT_SUCCESS) {
        auto icdCommandLists = icdFence->clearExecutedCommandListsPointers();

        for (const auto &commandList : icdCommandLists) {
            const auto icdCommandList = static_cast<IcdL0CommandList *>(commandList);
            const auto readResult = icdCommandList->readRequiredMemory();

            if (readResult != ZE_RESULT_SUCCESS) {
                return readResult;
            }
        }

        icdQueue->removeFromExecutedCommandLists(icdCommandLists);
    }

    return result;
}

ze_result_t zeCommandQueueSynchronize(ze_command_queue_handle_t hCommandQueue, uint64_t timeout) {
    auto icdCommandQueue = static_cast<IcdL0CommandQueue *>(hCommandQueue);
    auto lock = icdCommandQueue->lock();

    auto result = zeCommandQueueSynchronizeRpcHelper(hCommandQueue, timeout);
    if (result == ZE_RESULT_SUCCESS) {
        result = icdCommandQueue->readMemoryRequiredByCurrentlyExecutedCommandLists();
        icdCommandQueue->clearExecutedCommandListsPointers();
    }

    return result;
}

ze_result_t zeCommandQueueExecuteCommandLists(ze_command_queue_handle_t hCommandQueue, uint32_t numCommandLists, ze_command_list_handle_t *phCommandLists, ze_fence_handle_t hFence) {
    auto icdCommandQueue = static_cast<IcdL0CommandQueue *>(hCommandQueue);
    auto queueLock = icdCommandQueue->lock();

    icdCommandQueue->storeExecutedCommandListsPointers(numCommandLists, phCommandLists);

    if (hFence) {
        auto icdFence = static_cast<IcdL0Fence *>(hFence);
        icdFence->storeExecutedCommandListsPointers(numCommandLists, phCommandLists);
    }

    for (uint32_t i = 0u; i < numCommandLists; ++i) {
        const auto icdCommandList = static_cast<IcdL0CommandList *>(phCommandLists[i]);
        const auto writeResult = icdCommandList->writeRequiredMemory();

        if (writeResult != ZE_RESULT_SUCCESS) {
            return writeResult;
        }
    }

    queueLock.unlock();
    return zeCommandQueueExecuteCommandListsRpcHelper(hCommandQueue, numCommandLists, phCommandLists, hFence);
}

ze_result_t IcdL0CommandQueue::readMemoryRequiredByCurrentlyExecutedCommandLists() {
    for (const auto &commandList : currentlyExecutedCommandLists) {
        const auto icdCommandList = static_cast<IcdL0CommandList *>(commandList);
        const auto readResult = icdCommandList->readRequiredMemory();

        if (readResult != ZE_RESULT_SUCCESS) {
            return readResult;
        }
    }

    return ZE_RESULT_SUCCESS;
}

void IcdL0CommandQueue::storeExecutedCommandListsPointers(uint32_t numCommandLists, ze_command_list_handle_t *phCommandLists) {
    currentlyExecutedCommandLists.insert(currentlyExecutedCommandLists.end(),
                                         phCommandLists,
                                         phCommandLists + numCommandLists);
}

void IcdL0CommandQueue::removeFromExecutedCommandLists(const std::vector<ze_command_list_handle_t> &commandListsToRemove) {
    if (commandListsToRemove.empty()) {
        return;
    }

    auto first = std::find(currentlyExecutedCommandLists.begin(), currentlyExecutedCommandLists.end(), commandListsToRemove.front());
    auto last = std::find(currentlyExecutedCommandLists.begin(), currentlyExecutedCommandLists.end(), commandListsToRemove.back());
    if (first == currentlyExecutedCommandLists.end() && last == currentlyExecutedCommandLists.end()) {
        // Everything is fine. The range was synchronized in another call.
        return;
    }

    if (first == currentlyExecutedCommandLists.end() || last == currentlyExecutedCommandLists.end()) {
        log<Verbosity::error>("Could not remove executed command lists! Could not find all elements!");
        return;
    }

    const auto realLast = std::next(last);
    const auto foundRangeSize = std::distance(first, realLast);

    if (foundRangeSize != static_cast<int64_t>(commandListsToRemove.size())) {
        log<Verbosity::error>("Could not remove executed command lists! Invalid range! "
                              "Expected range size: %d, actual range size: %d",
                              static_cast<int>(commandListsToRemove.size()),
                              static_cast<int>(foundRangeSize));
        return;
    }

    currentlyExecutedCommandLists.erase(first, realLast);
}

std::vector<ze_command_list_handle_t> IcdL0Fence::clearExecutedCommandListsPointers() {
    std::lock_guard lock{currentlyExecutedCommandListsMutex};
    return std::move(currentlyExecutedCommandLists);
}

void IcdL0Fence::storeExecutedCommandListsPointers(uint32_t numCommandLists, ze_command_list_handle_t *phCommandLists) {
    std::lock_guard lock{currentlyExecutedCommandListsMutex};

    currentlyExecutedCommandLists.insert(currentlyExecutedCommandLists.end(),
                                         phCommandLists,
                                         phCommandLists + numCommandLists);
}

ze_result_t zeModuleGetKernelNames(ze_module_handle_t hModule, uint32_t *pCount, const char **pNames) {
    auto icdModule = static_cast<IcdL0Module *>(hModule);
    if (!icdModule) {
        return ZE_RESULT_ERROR_INVALID_NULL_HANDLE;
    }

    if (!pCount) {
        return ZE_RESULT_ERROR_INVALID_NULL_POINTER;
    }

    if (*pCount == 0) {
        return icdModule->getKernelNamesCount(pCount);
    }

    return icdModule->getKernelNames(pCount, pNames);
}

ze_result_t ImportedHostPointersManager::importExternalPointer(void *ptr, size_t size) {
    if (!ptr || !size) {
        log<Verbosity::error>("ImportedHostPointersManager::importExternalPointer(): passed NULL! ptr = %p, size = %zd", ptr, size);
        return ZE_RESULT_ERROR_INVALID_NULL_HANDLE;
    }

    const auto rangeBegin = reinterpret_cast<std::uintptr_t>(ptr);
    const auto rangeEnd = rangeBegin + size;

    Cal::Utils::AddressRange rangeToInsert{rangeBegin, rangeEnd};
    if (importedPointers.intersectsSubRanges(rangeToInsert)) {
        log<Verbosity::error>("Tried to import host pointer, which is already registered! ptr = %p, size = %zd", ptr, size);
        return ZE_RESULT_ERROR_INVALID_ARGUMENT;
    }

    importedPointers.insertSubRange(rangeToInsert);
    return ZE_RESULT_SUCCESS;
}

ze_result_t ImportedHostPointersManager::releaseImportedPointer(void *ptr) {
    if (!ptr) {
        log<Verbosity::error>("ImportedHostPointersManager::releaseImportedPointer(): passed NULL! ptr = %p", ptr);
        return ZE_RESULT_ERROR_INVALID_NULL_HANDLE;
    }

    const auto rangeBegin = reinterpret_cast<std::uintptr_t>(ptr);
    const auto rangeEnd = rangeBegin;

    Cal::Utils::AddressRange rangeToSearch{rangeBegin, rangeEnd};

    const auto ranges = importedPointers.getIntersectedSubRanges(rangeToSearch);
    if (ranges.empty()) {
        log<Verbosity::error>("Tried to remove a pointer, which had not been imported! ptr = %p", ptr);
        return ZE_RESULT_ERROR_INVALID_ARGUMENT;
    }

    if (ranges.size() > 1) {
        log<Verbosity::error>("More than one range intersects given address! Logic error - this should not happen!");
        return ZE_RESULT_ERROR_INVALID_ARGUMENT;
    }

    importedPointers.destroySubRange(ranges[0]->getBoundingRange());
    return ZE_RESULT_SUCCESS;
}

ze_result_t ImportedHostPointersManager::getHostPointerBaseAddress(void *ptr, void **baseAddress) {
    if (!baseAddress || !ptr) {
        log<Verbosity::error>("ImportedHostPointersManager::getHostPointerBaseAddress(): passed NULL! ptr = %p, baseAddress = %p", ptr, static_cast<void *>(baseAddress));
        return ZE_RESULT_ERROR_INVALID_NULL_HANDLE;
    }

    const auto rangeBegin = reinterpret_cast<std::uintptr_t>(ptr);
    const auto rangeEnd = rangeBegin;

    Cal::Utils::AddressRange rangeToSearch{rangeBegin, rangeEnd};

    const auto ranges = importedPointers.getIntersectedSubRanges(rangeToSearch);
    if (ranges.empty()) {
        *baseAddress = nullptr;
        return ZE_RESULT_SUCCESS;
    }

    if (ranges.size() > 1) {
        log<Verbosity::error>("More than one range intersects given address! Logic error - this should not happen!");
        return ZE_RESULT_ERROR_INVALID_ARGUMENT;
    }

    *baseAddress = ranges[0]->getBoundingRange().base();
    return ZE_RESULT_SUCCESS;
}

static ze_result_t zexDriverImportExternalPointer(ze_driver_handle_t hDriver, void *ptr, size_t size) {
    log<Verbosity::bloat>("Handling zexDriverImportExternalPointer() solely via ICD!");

    auto &instance = ImportedHostPointersManager::getInstance();
    auto instanceLock = instance.lock();

    return instance.importExternalPointer(ptr, size);
}

static ze_result_t zexDriverReleaseImportedPointer(ze_driver_handle_t hDriver, void *ptr) {
    log<Verbosity::bloat>("Handling zexDriverReleaseImportedPointer() solely via ICD!");

    auto &instance = ImportedHostPointersManager::getInstance();
    auto instanceLock = instance.lock();

    return instance.releaseImportedPointer(ptr);
}

static ze_result_t zexDriverGetHostPointerBaseAddress(ze_driver_handle_t hDriver, void *ptr, void **baseAddress) {
    log<Verbosity::bloat>("Handling zexDriverGetHostPointerBaseAddress() solely via ICD!");

    auto &instance = ImportedHostPointersManager::getInstance();
    auto instanceLock = instance.lock();

    return instance.getHostPointerBaseAddress(ptr, baseAddress);
}

ze_result_t zeDriverGetExtensionFunctionAddress(ze_driver_handle_t hDriver, const char *name, void **ppFunctionAddress) {
    if (!ppFunctionAddress || !name) {
        log<Verbosity::error>("zeDriverGetExtensionFunctionAddress(): Null passed! name = %p, ppFunctionAddress = %p",
                              static_cast<const void *>(name),
                              static_cast<void *>(ppFunctionAddress));

        return ZE_RESULT_ERROR_INVALID_NULL_POINTER;
    }

    if (0 == strcmp(name, "zexDriverImportExternalPointer")) {
        *ppFunctionAddress = reinterpret_cast<void *>(Cal::Icd::LevelZero::zexDriverImportExternalPointer);
    } else if (0 == strcmp(name, "zexDriverReleaseImportedPointer")) {
        *ppFunctionAddress = reinterpret_cast<void *>(Cal::Icd::LevelZero::zexDriverReleaseImportedPointer);
    } else if (0 == strcmp(name, "zexDriverGetHostPointerBaseAddress")) {
        *ppFunctionAddress = reinterpret_cast<void *>(Cal::Icd::LevelZero::zexDriverGetHostPointerBaseAddress);
    } else {
        *ppFunctionAddress = getL0ExtensionFuncionAddressRpcHelper(name);
        if (*ppFunctionAddress == nullptr) {
            log<Verbosity::error>("Unsupported extension function address requested for : %s", name);
        }
    }

    return *ppFunctionAddress ? ZE_RESULT_SUCCESS : ZE_RESULT_ERROR_INVALID_ARGUMENT;
}

bool IcdL0Platform::isZeAffinityMaskPresent() {
    std::call_once(parseZeAffinityMaskOnce, [this]() {
        auto zeAffinityMask = Cal::Utils::getCalEnv("ZE_AFFINITY_MASK");
        auto zeAffinityMaskDetected = zeAffinityMask != nullptr && zeAffinityMask[0] != 0;

        if (zeAffinityMaskDetected) {
            uint32_t numAllDevices = 0;
            std::vector<ze_device_handle_t> allDevices;
            std::vector<bool> selectedDevices;
            auto status = Cal::Icd::LevelZero::zeDeviceGetRpcHelper(this, &numAllDevices, nullptr);
            if (status != ZE_RESULT_SUCCESS) {
                return;
            }

            allDevices.resize(numAllDevices);
            status = Cal::Icd::LevelZero::zeDeviceGetRpcHelper(this, &numAllDevices, allDevices.data());
            if (status != ZE_RESULT_SUCCESS) {
                return;
            }
            selectedDevices.resize(numAllDevices);
            auto affinityMaskEntries = Cal::Utils::split(zeAffinityMask, ",");
            for (const auto &entry : affinityMaskEntries) {
                auto subEntries = Cal::Utils::split(entry, ".");
                auto deviceIndex = static_cast<uint32_t>(std::stoul(subEntries[0], nullptr, 0));
                if (deviceIndex < numAllDevices) {
                    selectedDevices[deviceIndex] = true;
                    if (subEntries.size() > 1) {
                        auto subDeviceIndex = static_cast<uint32_t>(std::stoul(subEntries[1], nullptr, 0));
                        auto device = static_cast<IcdL0Device *>(allDevices[deviceIndex]);
                        device->addSubDeviceToFilter(subDeviceIndex);
                    }
                }
            }

            for (auto i = 0u; i < numAllDevices; i++) {

                if (selectedDevices[i]) {
                    filteredDevices.push_back(allDevices[i]);
                }
            }
            zeAffinityMaskPresent = zeAffinityMaskDetected;
        }
    });

    return zeAffinityMaskPresent;
}

void IcdL0Device::addSubDeviceToFilter(uint32_t subDeviceIndex) {
    if (selectedDevices.size() < subDeviceIndex + 1) {
        selectedDevices.resize(subDeviceIndex + 1);
    }
    selectedDevices[subDeviceIndex] = true;
}
bool IcdL0Device::isZeAffinityMaskPresent() {
    std::call_once(parseZeAffinityMaskOnce, [this]() {
        if (!selectedDevices.empty()) {
            uint32_t numAllDevices = 0;
            std::vector<ze_device_handle_t> allDevices;
            auto status = Cal::Icd::LevelZero::zeDeviceGetSubDevicesRpcHelper(this, &numAllDevices, nullptr);
            if (status != ZE_RESULT_SUCCESS) {
                return;
            }

            allDevices.resize(numAllDevices);
            status = Cal::Icd::LevelZero::zeDeviceGetSubDevicesRpcHelper(this, &numAllDevices, allDevices.data());
            if (status != ZE_RESULT_SUCCESS) {
                return;
            }

            selectedDevices.resize(numAllDevices);

            for (auto i = 0u; i < numAllDevices; i++) {

                if (selectedDevices[i]) {
                    filteredDevices.push_back(allDevices[i]);
                }
            }
            zeAffinityMaskPresent = true;
        }
    });

    return zeAffinityMaskPresent;
}

ze_result_t IcdL0Module::getKernelNames(uint32_t *pCount, const char **pNames) {
    if (!wasKernelNamesQueried.load() && !queryKernelNames()) {
        return ZE_RESULT_ERROR_DEVICE_LOST;
    }

    const auto count = std::min(*pCount, static_cast<uint32_t>(kernelNames.size()));
    for (uint32_t i = 0; i < count; ++i) {
        pNames[i] = kernelNames[i].c_str();
    }

    *pCount = count;
    return ZE_RESULT_SUCCESS;
}

ze_result_t IcdL0Module::getKernelNamesCount(uint32_t *pCount) {
    if (!wasKernelNamesQueried.load() && !queryKernelNames()) {
        return ZE_RESULT_ERROR_DEVICE_LOST;
    }

    *pCount = static_cast<uint32_t>(kernelNames.size());
    return ZE_RESULT_SUCCESS;
}

bool IcdL0Module::queryKernelNames() {
    if (wasKernelNamesQueried.load()) {
        return true;
    }

    std::lock_guard lock{kernelNamesMutex};

    uint32_t totalLength{0};
    auto ret = Cal::Icd::LevelZero::zeModuleGetKernelNamesRpcHelper(this, &totalLength, nullptr);
    if (ZE_RESULT_SUCCESS != ret) {
        log<Verbosity::debug>("Failed to get total length of names of kernels from module from service!");
        return false;
    }

    std::vector<char> concatenatedNames;
    concatenatedNames.resize(totalLength);

    ret = Cal::Icd::LevelZero::zeModuleGetKernelNamesRpcHelper(this, &totalLength, concatenatedNames.data());
    if (ZE_RESULT_SUCCESS != ret) {
        log<Verbosity::debug>("Failed to get concatenated names of kernels from module from service!");
        return false;
    }

    populateKernelNames(concatenatedNames);

    wasKernelNamesQueried.store(true);
    return true;
}

void IcdL0Module::populateKernelNames(const std::vector<char> &buffer) {
    const auto data = buffer.data();

    auto lastBegin = 0u;
    for (auto i = 0u; i < buffer.size(); ++i) {
        if (buffer[i] == '\0') {
            kernelNames.emplace_back(data + lastBegin, data + i);
            lastBegin = i + 1;
        }
    }
}

bool IcdL0Module::removeGlobalPointer() {
    auto globalPtrLock = globalPointers.lock();
    for (auto &ptr : globalPointers.ptrList) {
        Cal::Icd::icdGlobalState.getL0Platform()->removeGlobalPointer(ptr);
    }
    globalPointers.ptrList.clear();
    return true;
}

bool IcdL0Module::recordGlobalPointer(void *ptr) {
    // Add to list if ptr is unique
    auto globalPtrLock = globalPointers.lock();
    if (std::find(globalPointers.ptrList.begin(), globalPointers.ptrList.end(), ptr) == globalPointers.ptrList.end()) {
        globalPointers.ptrList.push_back(ptr);
    } else {
        return true;
    }
    globalPtrLock.unlock();
    Cal::Icd::icdGlobalState.getL0Platform()->recordGlobalPointer(ptr);
    return true;
}

bool IcdL0Device::patchDeviceName(ze_device_properties_t &properties) {
    const auto nullTerminator = std::find(std::begin(properties.name), std::end(properties.name), '\0');
    if (nullTerminator == std::begin(properties.name)) {
        log<Verbosity::critical>("Cannot patch device name! It is empty!");
        return false;
    }

    static constexpr char deviceNameSuffix[] = " (aggregated)";
    static constexpr auto requiredSpace = std::distance(std::begin(deviceNameSuffix), std::end(deviceNameSuffix));

    const auto availableSpace = std::distance(nullTerminator, std::end(properties.name));
    if (availableSpace < requiredSpace) {
        log<Verbosity::critical>("Cannot patch device name with '(aggregated)' suffix! Not enough space!");
        return false;
    }

    std::copy(std::begin(deviceNameSuffix), std::end(deviceNameSuffix), nullTerminator);
    return true;
}

template <typename RemoteL0ObjectT, typename LocalL0ObjectT>
void objectCleanup(void *remote, void *local) {
    Cal::Icd::icdGlobalState.getL0Platform()->removeObjectFromMap(static_cast<RemoteL0ObjectT>(remote), static_cast<LocalL0ObjectT>(local));
}

template void objectCleanup<ze_device_handle_t, IcdL0Device *>(void *, void *);
template void objectCleanup<ze_context_handle_t, IcdL0Context *>(void *, void *);
template void objectCleanup<ze_command_queue_handle_t, IcdL0CommandQueue *>(void *, void *);
template void objectCleanup<ze_command_list_handle_t, IcdL0CommandList *>(void *, void *);
template void objectCleanup<ze_module_handle_t, IcdL0Module *>(void *, void *);
template void objectCleanup<ze_module_build_log_handle_t, IcdL0ModuleBuildLog *>(void *, void *);
template void objectCleanup<ze_kernel_handle_t, IcdL0Kernel *>(void *, void *);
template void objectCleanup<ze_event_handle_t, IcdL0Event *>(void *, void *);
template void objectCleanup<ze_event_pool_handle_t, IcdL0EventPool *>(void *, void *);
template void objectCleanup<ze_fence_handle_t, IcdL0Fence *>(void *, void *);

} // namespace LevelZero
} // namespace Icd
} // namespace Cal
