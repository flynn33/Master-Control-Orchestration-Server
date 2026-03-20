// Master Control Program
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#pragma once

#include "MasterControl/MasterControlContracts.h"

#include <memory>
#include <string>

namespace MasterControl {

class MasterControlApplication final {
public:
    MasterControlApplication();
    ~MasterControlApplication();

    MasterControlApplication(const MasterControlApplication&) = delete;
    MasterControlApplication& operator=(const MasterControlApplication&) = delete;

    bool initialize();
    void shutdown();
    void requestStop();
    int runInteractive();

    std::string browserUrl() const;
    DashboardSnapshot snapshot();
    OperationResult applyConfigurationJson(const std::string& requestBody,
                                           bool confirmUnsafeChanges);
    OperationResult upsertProviderJson(const std::string& requestBody);
    OperationResult upsertProviderCredentialsJson(const std::string& requestBody);
    OperationResult upsertSubAgentJson(const std::string& requestBody);
    OperationResult removeSubAgentJson(const std::string& requestBody);
    OperationResult upsertSubAgentGroupJson(const std::string& requestBody);
    OperationResult removeSubAgentGroupJson(const std::string& requestBody);
    OperationResult upsertProviderAssignmentJson(const std::string& requestBody);
    ProviderExecutionRecord executeProviderTaskJson(const std::string& requestBody);
    OperationResult installPackageJson(const std::string& requestBody);
    OperationResult installRepoJson(const std::string& requestBody);
    OperationResult installZipJson(const std::string& requestBody);
    BeaconAdvertisement beaconAdvertisement() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace MasterControl
