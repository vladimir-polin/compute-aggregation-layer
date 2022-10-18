/*
 * Copyright (C) 2022 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#define FULL_VERBOSITY_IN_RELEASE_BUILD 1
#define ENABLE_BLOATED_VERBOSITY 1

#include "cal.h"
#include "level_zero/ze_api.h"
#include "shared/log.h"
#include "test/utils/assertions.h"
#include "test/utils/dynamic_library.h"
#include "test/utils/l0_common_steps.h"

#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

static constexpr const char *simpleAppSource =
    R"(__kernel void CopyBuffer(__global unsigned int *src, __global unsigned int *dst) {
    int id = (int)get_global_id(0);
    dst[id] = src[id];
}
__kernel void DoubleVals(__global unsigned int *src, __global unsigned int *dst) {
    int id = (int)get_global_id(0);
    dst[id] = 2 * src[id];
})";

bool generateSpirv(std::vector<uint8_t> &spirv) {
    log<Verbosity::info>("Compiling simple kernels to SPIR-V via libocloc.so!");

    Cal::Testing::Utils::DynamicLibrary oclocLib{"libocloc.so"};
    if (!oclocLib.isLoaded()) {
        log<Verbosity::error>("Could not find libocloc.so!");
        return false;
    }

    using OclocInvokeFunT = int(unsigned int, const char *[], const uint32_t, const uint8_t **, const uint64_t *, const char **, const uint32_t,
                                const uint8_t **, const uint64_t *, const char **, uint32_t *, uint8_t ***, uint64_t **, char ***);

    auto *oclocInvoke = oclocLib.getFunction<OclocInvokeFunT>("oclocInvoke");
    if (!oclocInvoke) {
        log<Verbosity::error>("Cannot find oclocInvoke() function inside libocloc.so!");
        return false;
    }

    using OclocFreeOutputFunT = int(uint32_t *, uint8_t ***, uint64_t **, char ***);

    auto *oclocFreeOutput = oclocLib.getFunction<OclocFreeOutputFunT>("oclocFreeOutput");
    if (!oclocFreeOutput) {
        log<Verbosity::error>("Cannot find oclocFreeOutput() function inside libocloc.so!");
        return false;
    }

    std::array oclocArgs = {"ocloc", "-file", "simple_app.cl", "-spv_only"};
    std::array sources = {reinterpret_cast<const unsigned char *>(simpleAppSource)};
    std::array sourcesLengths = {strlen(simpleAppSource) + 1};
    std::array sourcesNames = {"simple_app.cl"};

    uint32_t numOutputs = 0U;
    uint8_t **outputs = nullptr;
    uint64_t *ouputLengths = nullptr;
    char **outputNames = nullptr;

    const auto compilationResult = oclocInvoke(oclocArgs.size(), oclocArgs.data(),
                                               sources.size(), sources.data(),
                                               sourcesLengths.data(), sourcesNames.data(),
                                               0, nullptr, nullptr, nullptr,
                                               &numOutputs, &outputs, &ouputLengths, &outputNames);
    if (compilationResult != 0) {
        log<Verbosity::error>("Compilation via ocloc lib has failed! Error code = %d", static_cast<int>(compilationResult));
        return false;
    }

    unsigned char *spirV = nullptr;
    size_t spirVlen = 0;

    for (unsigned int i = 0; i < numOutputs; ++i) {
        auto nameLen = strlen(outputNames[i]);
        if ((nameLen > 4) && (strstr(&outputNames[i][nameLen - 4], ".spv") != nullptr)) {
            spirV = outputs[i];
            spirVlen = ouputLengths[i];
            break;
        }
    }

    if (!spirV) {
        log<Verbosity::error>("Could not get generated SPIR-V file!");
        return false;
    }

    spirv.resize(spirVlen);
    std::memcpy(spirv.data(), spirV, spirVlen);

    oclocFreeOutput(&numOutputs, &outputs, &ouputLengths, &outputNames);
    return true;
}

bool createModule(ze_context_handle_t context, ze_device_handle_t device, const std::vector<uint8_t> &binary, ze_module_format_t binaryFormat, ze_module_handle_t &module) {
    log<Verbosity::info>("Creating module via zeModuleCreate()!");

    ze_module_desc_t moduleDescription = {
        ZE_STRUCTURE_TYPE_MODULE_DESC,
        nullptr,
        binaryFormat,
        binary.size(),
        binary.data(),
        "-Wno-recompiled-from-ir",
        nullptr};

    bool shouldFinishAfterBuildLog{false};
    ze_module_build_log_handle_t buildLog{};

    auto zeModuleCreateResult = zeModuleCreate(context, device, &moduleDescription, &module, &buildLog);
    if (zeModuleCreateResult != ZE_RESULT_SUCCESS) {
        log<Verbosity::error>("zeModuleCreate() has failed! Error code: %d", static_cast<int>(zeModuleCreateResult));
        shouldFinishAfterBuildLog = true;
    }

    log<Verbosity::info>("Getting build log size!");

    size_t buildLogSize{0};
    auto zeModuleBuildLogGetStringResult = zeModuleBuildLogGetString(buildLog, &buildLogSize, nullptr);
    if (zeModuleBuildLogGetStringResult != ZE_RESULT_SUCCESS) {
        log<Verbosity::error>("zeModuleBuildLogGetString() call has failed! Error code = %d", static_cast<int>(zeModuleBuildLogGetStringResult));
        return false;
    }

    log<Verbosity::info>("Build log size is %zd!", buildLogSize);
    if (buildLogSize > 0) {
        log<Verbosity::info>("Getting build log!");

        std::string buildLogStr{};
        buildLogStr.resize(buildLogSize + 1);

        zeModuleBuildLogGetStringResult = zeModuleBuildLogGetString(buildLog, &buildLogSize, buildLogStr.data());
        if (zeModuleBuildLogGetStringResult != ZE_RESULT_SUCCESS) {
            log<Verbosity::error>("zeModuleBuildLogGetString() call has failed! Error code = %d", static_cast<int>(zeModuleBuildLogGetStringResult));
            return false;
        }

        log<Verbosity::info>("Build log : %s", buildLogStr.c_str());
    } else {
        log<Verbosity::info>("Build log is empty!");
    }

    log<Verbosity::info>("Destroying module build log via zeModuleBuildLogDestroy()!");

    auto zeModuleBuildLogDestroyResult = zeModuleBuildLogDestroy(buildLog);
    if (zeModuleBuildLogDestroyResult != ZE_RESULT_SUCCESS) {
        log<Verbosity::error>("zeModuleBuildLogDestroy() call has failed! Error code = %d", static_cast<int>(zeModuleBuildLogDestroyResult));
        return false;
    }

    log<Verbosity::info>("Destruction of L0 module build log has been successful!");
    if (shouldFinishAfterBuildLog) {
        return false;
    }

    log<Verbosity::info>("Module creation succeeded! Module = %p", static_cast<void *>(module));
    return true;
}

bool destroyModule(ze_module_handle_t &module) {
    const auto zeModuleDestroyResult = zeModuleDestroy(module);
    if (zeModuleDestroyResult != ZE_RESULT_SUCCESS) {
        log<Verbosity::error>("zeModuleDestroy() call has failed for moduleHandle! Error code = %d", static_cast<int>(zeModuleDestroyResult));
        return false;
    }

    module = nullptr;
    log<Verbosity::info>("Module has been destroyed!");

    return true;
}

bool getNativeBinary(std::vector<uint8_t> &nativeBinary, ze_module_handle_t module) {
    log<Verbosity::info>("Getting native binary size from the module!");

    size_t nativeBinarySize{0};
    auto zeModuleGetNativeBinaryResult = zeModuleGetNativeBinary(module, &nativeBinarySize, nullptr);
    if (zeModuleGetNativeBinaryResult != ZE_RESULT_SUCCESS) {
        log<Verbosity::error>("Cannot get the size of native binary from module! Error code = %d", static_cast<int>(zeModuleGetNativeBinaryResult));
        return false;
    }

    log<Verbosity::info>("Retrieved size of native binary is : %zd", nativeBinarySize);
    if (nativeBinarySize == 0) {
        return true;
    }

    log<Verbosity::info>("Retrieving native binary from module!");
    nativeBinary.resize(nativeBinarySize);

    zeModuleGetNativeBinaryResult = zeModuleGetNativeBinary(module, &nativeBinarySize, nativeBinary.data());
    if (zeModuleGetNativeBinaryResult != ZE_RESULT_SUCCESS) {
        log<Verbosity::error>("Cannot get native binary from module! Error code = %d", zeModuleGetNativeBinaryResult);
        return false;
    }

    log<Verbosity::info>("Retrieved native binary from module!");
    return true;
}

bool getKernelNames(ze_module_handle_t module) {
    log<Verbosity::info>("Getting count of available kernels in module!");

    uint32_t kernelNamesCount{0};
    auto zeModuleGetKernelNamesResult = zeModuleGetKernelNames(module, &kernelNamesCount, nullptr);
    if (zeModuleGetKernelNamesResult != ZE_RESULT_SUCCESS) {
        log<Verbosity::error>("zeModuleGetKernelNames() call has failed! Error code = %d", static_cast<int>(zeModuleGetKernelNamesResult));
        return false;
    }

    log<Verbosity::info>("Count of kernels in module is: %d!", static_cast<int>(kernelNamesCount));
    if (kernelNamesCount == 0) {
        return true;
    }

    log<Verbosity::info>("Getting available kernel names from module!");

    std::vector<const char *> kernelNames{};
    kernelNames.resize(kernelNamesCount);

    zeModuleGetKernelNamesResult = zeModuleGetKernelNames(module, &kernelNamesCount, kernelNames.data());
    if (zeModuleGetKernelNamesResult != ZE_RESULT_SUCCESS) {
        log<Verbosity::error>("zeModuleGetKernelNames() call has failed! Error code = %d", static_cast<int>(zeModuleGetKernelNamesResult));
        return false;
    }

    log<Verbosity::info>("Kernel names from module are:");
    for (const auto &name : kernelNames) {
        log<Verbosity::info>("%s", name);
    }

    return true;
}

bool getModuleProperties(ze_module_handle_t module) {
    log<Verbosity::info>("Getting module properties via zeModuleGetProperties()");

    ze_module_properties_t moduleProps = {ZE_STRUCTURE_TYPE_MODULE_PROPERTIES};
    const auto zeModuleGetPropertiesResult = zeModuleGetProperties(module, &moduleProps);
    if (zeModuleGetPropertiesResult != ZE_RESULT_SUCCESS) {
        log<Verbosity::error>("zeModuleGetProperties() call has failed! Error code = %d", static_cast<int>(zeModuleGetPropertiesResult));
        return false;
    }

    log<Verbosity::info>("Module properties flags are: %d", static_cast<int>(moduleProps.flags));
    return true;
}

bool getNonexistentFunctionPointer(ze_module_handle_t module) {
    log<Verbosity::info>("Getting function pointer to non-existing function from module via zeModuleGetFunctionPointer!");

    void *nonexistingFunctionPtr{nullptr};
    const auto zeModuleGetFunctionPointerResult = zeModuleGetFunctionPointer(module, "SomeNonexistingFunction", &nonexistingFunctionPtr);
    if (zeModuleGetFunctionPointerResult == ZE_RESULT_ERROR_INVALID_FUNCTION_NAME && nonexistingFunctionPtr == nullptr) {
        log<Verbosity::info>("zeModuleGetFunctionPointer() correctly returned ZE_RESULT_ERROR_INVALID_FUNCTION_NAME!");
        return true;
    }

    log<Verbosity::error>("zeModuleGetFunctionPointer() call has failed! Error code = %d, nonexistingFunctionPtr = %p",
                          static_cast<int>(zeModuleGetFunctionPointerResult),
                          nonexistingFunctionPtr);
    return false;
}

bool getNonexistentGlobalVariable(ze_module_handle_t module) {
    log<Verbosity::info>("Getting nonexistent global pointer from module");

    const char *nonexistentGlobalVariable = "nonexisting_global_variable";
    size_t globalSize{};
    void *globalPtr{nullptr};

    const auto zeModuleGetGlobalPointerResult = zeModuleGetGlobalPointer(module, nonexistentGlobalVariable, &globalSize, &globalPtr);
    if (zeModuleGetGlobalPointerResult != ZE_RESULT_SUCCESS && globalPtr == nullptr) {
        log<Verbosity::info>("zeModuleGetGlobalPointer() correctly could not get a pointer for nonexistent_global_variable!");
        return true;
    }

    log<Verbosity::error>("zeModuleGetGlobalPointer() unexpectedly returned a pointer for nonexistent_global_variable! This should not happen! "
                          "Error code = %d, nonexistingGlobalVariable = %p",
                          static_cast<int>(zeModuleGetGlobalPointerResult),
                          globalPtr);
    return false;
}

bool createKernel(ze_module_handle_t module, ze_kernel_handle_t &kernel, const char *kernelName) {
    ze_kernel_desc_t kernelDesc = {ZE_STRUCTURE_TYPE_KERNEL_DESC, nullptr, 0, kernelName};

    const auto zeKernelCreateResult = zeKernelCreate(module, &kernelDesc, &kernel);
    if (zeKernelCreateResult != ZE_RESULT_SUCCESS) {
        log<Verbosity::error>("zeKernelCreate() call has failed! Error code = %d", static_cast<int>(zeKernelCreateResult));
        return false;
    }

    log<Verbosity::info>("Kernel has been created successfully!");
    return true;
}

bool destroyKernel(ze_kernel_handle_t &kernel) {
    const auto zeKernelDestroyResult = zeKernelDestroy(kernel);
    if (zeKernelDestroyResult != ZE_RESULT_SUCCESS) {
        log<Verbosity::error>("zeKernelDestroy() call has failed! Error code = %d", static_cast<int>(zeKernelDestroyResult));
        return false;
    }

    kernel = nullptr;
    log<Verbosity::info>("Kernel has been successfully destroyed!");

    return true;
}

bool getKernelName(ze_kernel_handle_t kernel) {
    log<Verbosity::info>("Getting kernel name size via zeKernelGetName()");

    size_t kernelNameSize{};
    const auto zeKernelGetNameSizeResult = zeKernelGetName(kernel, &kernelNameSize, nullptr);
    if (zeKernelGetNameSizeResult != ZE_RESULT_SUCCESS) {
        log<Verbosity::error>("zeKernelGetName() call has failed! Error code = %d", static_cast<int>(zeKernelGetNameSizeResult));
        return false;
    }

    log<Verbosity::info>("The size of kernel name is %zd", kernelNameSize);
    if (kernelNameSize == 0) {
        return true;
    }

    log<Verbosity::info>("Getting kernel name via zeKernelGetName()");

    std::string kernelName{};
    kernelName.resize(kernelNameSize + 1);

    const auto zeKernelGetNameResult = zeKernelGetName(kernel, &kernelNameSize, kernelName.data());
    if (zeKernelGetNameResult != ZE_RESULT_SUCCESS) {
        log<Verbosity::error>("zeKernelGetName() call has failed! Error code = %d", static_cast<int>(zeKernelGetNameResult));
        return false;
    }

    log<Verbosity::info>("Kernel name is: %s", kernelName.c_str());
    return true;
}

bool getIndirectAccessFlags(ze_kernel_handle_t kernel, ze_kernel_indirect_access_flags_t &indirectAccessFlags) {
    log<Verbosity::info>("Getting indirect access flags via zeKernelGetIndirectAccess()!");

    const auto zeKernelGetIndirectAccessResult = zeKernelGetIndirectAccess(kernel, &indirectAccessFlags);
    if (zeKernelGetIndirectAccessResult != ZE_RESULT_SUCCESS) {
        log<Verbosity::error>("zeKernelGetIndirectAccess() call has failed! Error code = %d", static_cast<int>(zeKernelGetIndirectAccessResult));
        return false;
    }

    log<Verbosity::info>("Indirect access flags: %d", static_cast<int>(indirectAccessFlags));
    return true;
}

bool setIndirectAccessFlags(ze_kernel_handle_t kernel, ze_kernel_indirect_access_flags_t indirectAccessFlags) {
    log<Verbosity::info>("Setting indirect access flags via zeKernelSetIndirectAccess()!");

    const auto zeKernelSetIndirectAccessResult = zeKernelSetIndirectAccess(kernel, indirectAccessFlags);
    if (zeKernelSetIndirectAccessResult != ZE_RESULT_SUCCESS) {
        log<Verbosity::error>("zeKernelSetIndirectAccess() call has failed! Error code = %d", static_cast<int>(zeKernelSetIndirectAccessResult));
        return false;
    }

    log<Verbosity::info>("Indirect access flags have been set successfully!");
    return true;
}

bool getTotalGroupCount(ze_kernel_handle_t kernel) {
    log<Verbosity::info>("Getting total group count via zeKernelSuggestMaxCooperativeGroupCount()");

    uint32_t totalGroupCount{};
    const auto zeKernelSuggestMaxCooperativeGroupCountResult = zeKernelSuggestMaxCooperativeGroupCount(kernel, &totalGroupCount);
    if (zeKernelSuggestMaxCooperativeGroupCountResult != ZE_RESULT_SUCCESS) {
        log<Verbosity::error>("zeKernelSuggestMaxCooperativeGroupCount() call has failed! Error code = %d", static_cast<int>(zeKernelSuggestMaxCooperativeGroupCountResult));
        return false;
    }

    log<Verbosity::info>("Total group count is: %d", static_cast<int>(totalGroupCount));
    return true;
}

bool getKernelProperties(ze_kernel_handle_t kernel) {
    log<Verbosity::info>("Getting kernel properties via zeKernelGetProperties()!");

    ze_kernel_properties_t kernelProperties{};
    const auto zeKernelGetPropertiesResult = zeKernelGetProperties(kernel, &kernelProperties);
    if (zeKernelGetPropertiesResult != ZE_RESULT_SUCCESS) {
        log<Verbosity::error>("zeKernelGetProperties() call has failed! Error code = %d", static_cast<int>(zeKernelGetPropertiesResult));
        return false;
    }

    log<Verbosity::info>("Number of kernel arguments read from properties is: %d", static_cast<int>(kernelProperties.numKernelArgs));
    return true;
}

bool setCacheConfig(ze_kernel_handle_t kernel, ze_cache_config_flag_t config) {
    log<Verbosity::info>("Calling zeKernelSetCacheConfig() to set config flags = %d!", static_cast<int>(config));

    const auto zeKernelSetCacheConfigResult = zeKernelSetCacheConfig(kernel, config);
    if (zeKernelSetCacheConfigResult != ZE_RESULT_SUCCESS) {
        log<Verbosity::error>("zeKernelSetCacheConfig() call has failed! Error code = %d", static_cast<int>(zeKernelSetCacheConfigResult));
        return false;
    }

    log<Verbosity::info>("zeKernelSetCacheConfig() call has succeeded!");
    return true;
}

bool suggestGroupSize(ze_kernel_handle_t kernel,
                      uint32_t xSize, uint32_t ySize, uint32_t zSize,
                      uint32_t &suggestedGroupSizeX, uint32_t &suggestedGroupSizeY, uint32_t &suggestedGroupSizeZ) {
    log<Verbosity::info>("Trying to get suggested group size for processing (%d x %d x %d) problem.",
                         static_cast<int>(xSize), static_cast<int>(ySize), static_cast<int>(zSize));

    const auto zeKernelSuggestGroupSizeResult = zeKernelSuggestGroupSize(kernel, xSize, ySize, zSize, &suggestedGroupSizeX, &suggestedGroupSizeY, &suggestedGroupSizeZ);
    if (zeKernelSuggestGroupSizeResult != ZE_RESULT_SUCCESS) {
        log<Verbosity::error>("zeKernelSuggestGroupSize() call has failed! Error code = %d", static_cast<int>(zeKernelSuggestGroupSizeResult));
        return false;
    }

    log<Verbosity::info>("The suggested sizes are: (%d, %d, %d)", static_cast<int>(suggestedGroupSizeX), static_cast<int>(suggestedGroupSizeY), static_cast<int>(suggestedGroupSizeZ));
    return true;
}

bool setGroupSize(ze_kernel_handle_t kernel, uint32_t suggestedGroupSizeX, uint32_t suggestedGroupSizeY, uint32_t suggestedGroupSizeZ) {
    log<Verbosity::info>("Setting kernel group size via zeKernelSetGroupSize() for CopyBuffer kernel!");

    const auto zeKernelSetGroupSizeResult = zeKernelSetGroupSize(kernel, suggestedGroupSizeX, suggestedGroupSizeY, suggestedGroupSizeZ);
    if (zeKernelSetGroupSizeResult != ZE_RESULT_SUCCESS) {
        log<Verbosity::error>("zeKernelSetGroupSize() call has failed! Error code: %d", static_cast<int>(zeKernelSetGroupSizeResult));
        return false;
    }

    log<Verbosity::info>("Call to zeKernelSetGroupSize() has been successful!");
    return true;
}

bool setKernelArgument(ze_kernel_handle_t kernel, uint32_t argIndex, size_t argSize, const void *arg) {
    const auto zeKernelSetArgumentValueResult = zeKernelSetArgumentValue(kernel, argIndex, argSize, arg);
    if (zeKernelSetArgumentValueResult != ZE_RESULT_SUCCESS) {
        log<Verbosity::error>("zeKernelSetArgumentValue() call has failed! Error code: %d", static_cast<int>(zeKernelSetArgumentValueResult));
        return false;
    }

    log<Verbosity::info>("Argument has been set correctly!");
    return true;
}

bool appendLaunchKernel(ze_command_list_handle_t cmdList, ze_kernel_handle_t kernel, ze_group_count_t &launchArgs, ze_event_handle_t signalEvent) {
    const auto zeCommandListAppendLaunchKernelResult = zeCommandListAppendLaunchKernel(cmdList,
                                                                                       kernel,
                                                                                       &launchArgs,
                                                                                       signalEvent,
                                                                                       0,
                                                                                       nullptr);
    if (zeCommandListAppendLaunchKernelResult != ZE_RESULT_SUCCESS) {
        log<Verbosity::error>("zeCommandListAppendLaunchKernel() call has failed! Error code = %d", static_cast<int>(zeCommandListAppendLaunchKernelResult));
        return false;
    }

    log<Verbosity::info>("Launch kernel operation appended successfully!");
    return true;
}

bool setGlobalOffset(ze_kernel_handle_t kernel, uint32_t offsetX, uint32_t offsetY, uint32_t offsetZ) {
    const auto zeKernelSetGlobalOffsetExpResult = zeKernelSetGlobalOffsetExp(kernel, offsetX, offsetY, offsetZ);
    if (zeKernelSetGlobalOffsetExpResult != ZE_RESULT_SUCCESS) {
        log<Verbosity::error>("zeKernelSetGlobalOffsetExp() call has failed! Error code = %d", static_cast<int>(zeKernelSetGlobalOffsetExpResult));
        return false;
    }

    log<Verbosity::info>("Kernel global offsets set to (%d, %d, %d)", static_cast<int>(offsetX), static_cast<int>(offsetY), static_cast<int>(offsetZ));
    return true;
}

bool verifyCopyBufferResults(const void *sourceCopyBuffer, const void *destinationCopyBuffer, size_t bufferSize) {
    log<Verbosity::info>("Validating results of CopyBuffer kernel execution!");

    const auto src = static_cast<const char *>(sourceCopyBuffer);
    const auto dst = static_cast<const char *>(destinationCopyBuffer);

    for (size_t i = 0; i < bufferSize; ++i) {
        if (src[i] != dst[i]) {
            log<Verbosity::error>("Destination of CopyBuffer contains invalid value! Expected: %d, Actual: %d at index %zd.",
                                  static_cast<int>(src[i]), static_cast<int>(dst[i]), i);
            return false;
        }
    }

    log<Verbosity::info>("CopyBuffer passed validation!");
    return true;
}

bool verifyDoubleValsResults(const void *sourceDoubleVals, const void *destinationDoubleVals, size_t bufferSize) {
    log<Verbosity::info>("Validating results of DoubleVals kernel execution!");

    const auto src = static_cast<const unsigned int *>(sourceDoubleVals);
    const auto dst = static_cast<const unsigned int *>(destinationDoubleVals);
    const auto count = bufferSize / sizeof(unsigned int);

    for (size_t i = 0; i < count; ++i) {
        if ((2 * src[i]) != dst[i]) {
            log<Verbosity::error>("Destination of DoubleVals contains invalid value! Expected: %d, Actual: %d at index %zd.", 2 * src[i], dst[i], i);
            return false;
        }
    }

    log<Verbosity::info>("DoubleVals passed validation!");
    return true;
}

bool queryKernelTimestamp(ze_event_handle_t event, ze_device_handle_t device) {
    log<Verbosity::info>("Querying kernel timestamp via zeEventQueryKernelTimestamp()");

    ze_kernel_timestamp_result_t kernelTsResults{};
    const auto zeEventQueryKernelTimestampsResult = zeEventQueryKernelTimestamp(event, &kernelTsResults);
    if (zeEventQueryKernelTimestampsResult != ZE_RESULT_SUCCESS) {
        log<Verbosity::error>("zeEventQueryKernelTimestamp() call has failed! Error code = %d", static_cast<int>(zeEventQueryKernelTimestampsResult));
        return false;
    }

    log<Verbosity::info>("Getting timerResolution via zeDeviceGetProperties()");

    ze_device_properties_t deviceProperties = {ZE_STRUCTURE_TYPE_DEVICE_PROPERTIES};
    const auto zeDeviceGetPropertiesResult = zeDeviceGetProperties(device, &deviceProperties);
    if (zeDeviceGetPropertiesResult != ZE_RESULT_SUCCESS) {
        log<Verbosity::error>("zeDeviceGetProperties() call has failed! Error code = %d", static_cast<int>(zeDeviceGetPropertiesResult));
        return false;
    }

    uint64_t timerResolution = deviceProperties.timerResolution;
    uint64_t kernelDuration = kernelTsResults.context.kernelEnd - kernelTsResults.context.kernelStart;

    log<Verbosity::info>("Kernel timestamp statistics: \n"
                         " Global start : %lx cycles\n"
                         " Kernel start:  %lx cycles\n"
                         " Kernel end: %lx cycles\n"
                         " Global end: %lx cycles\n"
                         " timerResolution clock: %d ns\n"
                         " Kernel duration : %d cycles",
                         kernelTsResults.global.kernelStart, kernelTsResults.context.kernelStart, kernelTsResults.context.kernelEnd, kernelTsResults.global.kernelEnd, timerResolution,
                         kernelDuration);
    return true;
}

int main(int argc, const char *argv[]) {
    using namespace Cal::Testing::Utils::LevelZero;

    Cal::Utils::initMaxDynamicVerbosity(Verbosity::debug);

    std::vector<ze_driver_handle_t> drivers{};
    std::vector<ze_device_handle_t> devices{};

    RUN_REQUIRED_STEP(initL0());
    RUN_REQUIRED_STEP(getDrivers(drivers));
    RUN_REQUIRED_STEP(getDevices(drivers[0], devices));

    ze_context_handle_t context{};
    RUN_REQUIRED_STEP(createContext(drivers[0], context));

    std::vector<uint8_t> spirv{};
    RUN_REQUIRED_STEP(generateSpirv(spirv));

    ze_module_handle_t module{};
    RUN_REQUIRED_STEP(createModule(context, devices[0], spirv, ZE_MODULE_FORMAT_IL_SPIRV, module));

    std::vector<uint8_t> nativeBinary{};
    RUN_REQUIRED_STEP(getNativeBinary(nativeBinary, module));

    ze_module_handle_t anotherModule{};
    RUN_REQUIRED_STEP(createModule(context, devices[0], nativeBinary, ZE_MODULE_FORMAT_NATIVE, anotherModule));

    RUN_REQUIRED_STEP(getKernelNames(module));
    RUN_REQUIRED_STEP(getModuleProperties(module));
    RUN_REQUIRED_STEP(getNonexistentFunctionPointer(module));
    RUN_REQUIRED_STEP(getNonexistentGlobalVariable(module));

    ze_kernel_handle_t copyBufferKernel{};
    RUN_REQUIRED_STEP(createKernel(module, copyBufferKernel, "CopyBuffer"));

    RUN_REQUIRED_STEP(getKernelName(copyBufferKernel));
    RUN_REQUIRED_STEP(getTotalGroupCount(copyBufferKernel));
    RUN_REQUIRED_STEP(getKernelProperties(copyBufferKernel));
    RUN_REQUIRED_STEP(setCacheConfig(copyBufferKernel, ZE_CACHE_CONFIG_FLAG_LARGE_SLM));

    ze_kernel_indirect_access_flags_t indirectAccessFlags{};
    RUN_REQUIRED_STEP(getIndirectAccessFlags(copyBufferKernel, indirectAccessFlags));
    RUN_REQUIRED_STEP(setIndirectAccessFlags(copyBufferKernel, indirectAccessFlags));

    constexpr int xSize{1024};
    constexpr int ySize{1};
    constexpr int zSize{1};

    uint32_t suggestedGroupSizeX{};
    uint32_t suggestedGroupSizeY{};
    uint32_t suggestedGroupSizeZ{};

    RUN_REQUIRED_STEP(suggestGroupSize(copyBufferKernel, xSize, ySize, zSize, suggestedGroupSizeX, suggestedGroupSizeY, suggestedGroupSizeZ));
    RUN_REQUIRED_STEP(setGroupSize(copyBufferKernel, suggestedGroupSizeX, suggestedGroupSizeY, suggestedGroupSizeZ));

    ze_kernel_handle_t doubleValsKernel{};
    RUN_REQUIRED_STEP(createKernel(module, doubleValsKernel, "DoubleVals"));
    RUN_REQUIRED_STEP(getKernelName(doubleValsKernel));
    RUN_REQUIRED_STEP(setGroupSize(doubleValsKernel, suggestedGroupSizeX, suggestedGroupSizeY, suggestedGroupSizeZ));

    uint32_t ordinal{};
    RUN_REQUIRED_STEP(getComputeQueueOrdinal(devices[0], ordinal));

    ze_command_queue_handle_t queue{};
    RUN_REQUIRED_STEP(createCommandQueue(context, devices[0], ordinal, queue));

    ze_command_list_handle_t cmdList{};
    RUN_REQUIRED_STEP(createCommandList(context, devices[0], ordinal, cmdList));

    constexpr uint32_t eventsCount{10};
    ze_event_pool_handle_t eventPool{};
    RUN_REQUIRED_STEP(createEventPool(context, eventsCount, &devices[0], 1, eventPool));

    constexpr size_t bufferSize{xSize * sizeof(unsigned int)};
    constexpr size_t alignment{8};

    void *sourceCopyBuffer{nullptr};
    void *destinationCopyBuffer{nullptr};

    RUN_REQUIRED_STEP(allocateHostMemory(context, bufferSize, alignment, sourceCopyBuffer));
    RUN_REQUIRED_STEP(allocateHostMemory(context, bufferSize, alignment, destinationCopyBuffer));

    std::memset(sourceCopyBuffer, 0xFF, bufferSize);
    std::memset(destinationCopyBuffer, 0xAA, bufferSize);

    RUN_REQUIRED_STEP(setKernelArgument(copyBufferKernel, 0, sizeof(sourceCopyBuffer), &sourceCopyBuffer));
    RUN_REQUIRED_STEP(setKernelArgument(copyBufferKernel, 1, sizeof(destinationCopyBuffer), &destinationCopyBuffer));

    ze_event_handle_t copyBufferFinishedEvent{};
    RUN_REQUIRED_STEP(createEvent(eventPool, 1, copyBufferFinishedEvent));

    ze_group_count_t launchArgs{};
    launchArgs.groupCountX = xSize / suggestedGroupSizeX;
    launchArgs.groupCountY = ySize / suggestedGroupSizeY;
    launchArgs.groupCountZ = zSize / suggestedGroupSizeZ;

    RUN_REQUIRED_STEP(appendLaunchKernel(cmdList, copyBufferKernel, launchArgs, copyBufferFinishedEvent));

    void *sourceDoubleVals{nullptr};
    void *destinationDoubleVals{nullptr};

    RUN_REQUIRED_STEP(allocateHostMemory(context, bufferSize, alignment, sourceDoubleVals));
    RUN_REQUIRED_STEP(allocateHostMemory(context, bufferSize, alignment, destinationDoubleVals));

    std::memset(sourceDoubleVals, 0x1, bufferSize);
    std::memset(destinationDoubleVals, 0x2, bufferSize);

    RUN_REQUIRED_STEP(setKernelArgument(doubleValsKernel, 0, sizeof(sourceDoubleVals), &sourceDoubleVals));
    RUN_REQUIRED_STEP(setKernelArgument(doubleValsKernel, 1, sizeof(destinationDoubleVals), &destinationDoubleVals));

    RUN_REQUIRED_STEP(setGlobalOffset(doubleValsKernel, 0, 0, 0));
    RUN_REQUIRED_STEP(appendLaunchKernel(cmdList, doubleValsKernel, launchArgs, nullptr));

    RUN_REQUIRED_STEP(closeCommandList(cmdList));
    RUN_REQUIRED_STEP(executeCommandLists(queue, 1, &cmdList, nullptr));
    RUN_REQUIRED_STEP(synchronizeCommandQueue(queue));

    RUN_REQUIRED_STEP(verifyCopyBufferResults(sourceCopyBuffer, destinationCopyBuffer, bufferSize));
    RUN_REQUIRED_STEP(verifyDoubleValsResults(sourceDoubleVals, destinationDoubleVals, bufferSize));

    RUN_REQUIRED_STEP(queryKernelTimestamp(copyBufferFinishedEvent, devices[0]));

    RUN_REQUIRED_STEP(destroyEvent(copyBufferFinishedEvent));
    RUN_REQUIRED_STEP(destroyEventPool(eventPool));
    RUN_REQUIRED_STEP(destroyCommandList(cmdList));
    RUN_REQUIRED_STEP(destroyCommandQueue(queue));

    RUN_REQUIRED_STEP(freeMemory(context, sourceDoubleVals));
    RUN_REQUIRED_STEP(freeMemory(context, destinationDoubleVals));

    RUN_REQUIRED_STEP(freeMemory(context, sourceCopyBuffer));
    RUN_REQUIRED_STEP(freeMemory(context, destinationCopyBuffer));

    RUN_REQUIRED_STEP(destroyKernel(doubleValsKernel));
    RUN_REQUIRED_STEP(destroyKernel(copyBufferKernel));

    RUN_REQUIRED_STEP(destroyModule(anotherModule));
    RUN_REQUIRED_STEP(destroyModule(module));

    RUN_REQUIRED_STEP(destroyContext(context));

    return 0;
}