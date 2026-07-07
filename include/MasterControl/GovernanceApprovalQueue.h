// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#pragma once

#include "MasterControl/MasterControlContracts.h"
#include "MasterControl/MasterControlModels.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace MasterControl {

// Applies an operator-approved deferred governance action exactly once. The
// approval queue owns the "exactly once" guarantee (via the pending-status
// guard); the executor performs the concrete, bounded mutation for the action
// kind and reports success/failure honestly. Unknown or unhandled kinds must
// fail rather than silently report success.
class IGovernanceActionExecutor {
public:
    virtual OperationResult execute(const GovernanceDeferredAction& action) = 0;
    virtual ~IGovernanceActionExecutor() = default;
};

// Phase 7 approval queue, extracted from MasterControlRuntime.cpp so the test
// suite can construct it directly. Mutations whose CLU outcome is
// RequiresOperatorApproval are staged here until an operator approves or
// rejects them. On approval the staged action is applied through the injected
// executor exactly once: approve() claims the action (status -> "approved")
// before invoking the executor, so a second approve() finds the action no
// longer pending and the mutation can never run twice. Approve/reject/execute
// transitions are recorded through the injected audit sink.
//
// Persistence across restarts is intentionally deferred - long-running
// deferrals are operationally suspect on a trusted LAN, so the queue lives in
// process memory only.
//
// The clock and audit sink are injected so the queue is a pure, header-only
// state machine with no translation-unit-local dependencies. The runtime wires
// in the canonical UTC clock and the LAN-client activity stream; unit tests
// inject deterministic doubles (or accept the no-op defaults).
class GovernanceApprovalQueueService final : public IGovernanceApprovalQueueService {
public:
    using Clock = std::function<std::string()>;
    using AuditSink = std::function<void(const std::string& kind,
                                         const std::string& subjectId,
                                         const std::string& message)>;

    explicit GovernanceApprovalQueueService(
        std::shared_ptr<IGovernanceActionExecutor> executor = nullptr,
        Clock clock = Clock{},
        AuditSink audit = AuditSink{})
        : executor_(std::move(executor))
        , clock_(clock ? std::move(clock) : Clock([]() { return std::string(); }))
        , audit_(audit ? std::move(audit)
                       : AuditSink([](const std::string&, const std::string&, const std::string&) {})) {}

    std::vector<GovernanceDeferredAction> listPending() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<GovernanceDeferredAction> pending;
        for (const auto& action : actions_) {
            if (action.status == "pending") {
                pending.push_back(action);
            }
        }
        return pending;
    }

    std::vector<GovernanceDeferredAction> listAll() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return actions_;
    }

    GovernanceDeferredAction stage(const GovernanceDeferredAction& input) override {
        std::lock_guard<std::mutex> lock(mutex_);
        GovernanceDeferredAction action = input;
        ++nextSequence_;
        action.id = "deferred-" + std::to_string(nextSequence_);
        action.status = "pending";
        action.createdAtUtc = clock_();
        actions_.push_back(action);
        audit_("governance-deferred",
               action.id,
               std::string("Staged ") + to_string(action.action) +
                   " for operator approval (actor=" + action.actor + ")");
        return action;
    }

    OperationResult approve(const std::string& deferredActionId,
                            const std::string& operatorActor) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto* action = findById(deferredActionId);
        if (action == nullptr) {
            return OperationResult{ false, false, "Deferred action not found." };
        }
        if (action->status != "pending") {
            return OperationResult{ false, false,
                "Deferred action is no longer pending (current status: " + action->status + ")." };
        }
        // Claim the action before executing so a concurrent approve() observes
        // a non-pending status and cannot trigger a second execution.
        action->status = "approved";
        action->decidedAtUtc = clock_();
        action->decidedBy = operatorActor.empty() ? std::string("operator") : operatorActor;
        const std::string actionKind = to_string(action->action);
        audit_("governance-approved",
               action->id,
               std::string("Operator approved deferred ") + actionKind + " for actor=" + action->actor);

        if (!executor_) {
            // No executor wired: the approval is recorded but nothing is
            // applied. The runtime always injects an executor; this path keeps
            // the queue usable in isolation (e.g. tests that only check state).
            return OperationResult{ true, false, "Deferred action approved." };
        }

        const GovernanceDeferredAction snapshot = *action;
        const OperationResult execution = executor_->execute(snapshot);
        // Re-find defensively. The executor must not mutate the queue; even so,
        // holding the lock and pushing nothing means actions_ cannot have
        // reallocated, so the pointer would still be valid.
        action = findById(deferredActionId);
        if (action != nullptr) {
            action->status = execution.succeeded ? "executed" : "execute-failed";
            if (!execution.succeeded && action->reason.empty()) {
                action->reason = execution.message;
            }
        }
        audit_(execution.succeeded ? "governance-executed" : "governance-execute-failed",
               deferredActionId,
               execution.succeeded
                   ? (std::string("Executed approved ") + actionKind)
                   : (std::string("Execution of approved ") + actionKind + " failed: " + execution.message));
        if (execution.succeeded) {
            return OperationResult{ true, false, "Deferred action approved and executed." };
        }
        return OperationResult{ false, false,
            "Deferred action approved but execution failed: " + execution.message };
    }

    OperationResult reject(const std::string& deferredActionId,
                           const std::string& operatorActor,
                           const std::string& reason) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto* action = findById(deferredActionId);
        if (action == nullptr) {
            return OperationResult{ false, false, "Deferred action not found." };
        }
        if (action->status != "pending") {
            return OperationResult{ false, false,
                "Deferred action is no longer pending (current status: " + action->status + ")." };
        }
        action->status = "rejected";
        action->decidedAtUtc = clock_();
        action->decidedBy = operatorActor.empty() ? std::string("operator") : operatorActor;
        action->reason = reason;
        audit_("governance-rejected",
               action->id,
               std::string("Operator rejected deferred ") + to_string(action->action) +
                   " (reason=" + reason + ")");
        return OperationResult{ true, false, "Deferred action rejected." };
    }

private:
    GovernanceDeferredAction* findById(const std::string& id) {
        for (auto& action : actions_) {
            if (action.id == id) {
                return &action;
            }
        }
        return nullptr;
    }

    std::shared_ptr<IGovernanceActionExecutor> executor_;
    Clock clock_;
    AuditSink audit_;
    mutable std::mutex mutex_;
    std::vector<GovernanceDeferredAction> actions_;
    std::uint64_t nextSequence_ = 0;
};

} // namespace MasterControl
