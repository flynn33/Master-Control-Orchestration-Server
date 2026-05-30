// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#pragma once

#include "MasterControl/MasterControlModels.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace MasterControl {

struct WorkflowValidationIssue final {
    std::string id;
    std::string message;
};

struct WorkflowReadinessCounts final {
    int ready = 0;
    int missing = 0;
    int invalid = 0;
    int disabled = 0;
};

inline bool isWorkflowSourceValid(const std::string& source) {
    return source == "manual" || source == "imported" || source == "starter-template";
}

inline bool isWorkflowStepKindValid(const std::string& kind) {
    return kind == "mcp-tool" ||
           kind == "sub-agent" ||
           kind == "approval" ||
           kind == "condition" ||
           kind == "wait" ||
           kind == "notification";
}

inline bool isWorkflowIdShapeValid(const std::string& value) {
    if (value.size() < 3 || value.size() > 128) {
        return false;
    }
    const auto isLowerAlphaNum = [](const unsigned char ch) {
        return std::isdigit(ch) || (ch >= 'a' && ch <= 'z');
    };
    if (!isLowerAlphaNum(static_cast<unsigned char>(value.front()))) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](const unsigned char ch) {
        return std::isdigit(ch) ||
               (ch >= 'a' && ch <= 'z') ||
               ch == '.' ||
               ch == '_' ||
               ch == '-';
    });
}

inline std::vector<WorkflowValidationIssue> validateWorkflowDefinition(const WorkflowDefinition& workflow) {
    std::vector<WorkflowValidationIssue> issues;
    if (!isWorkflowIdShapeValid(workflow.workflowId)) {
        issues.push_back({ "workflow.id.invalid", "Workflow id must match the documented lowercase id shape." });
    }
    if (workflow.displayName.empty()) {
        issues.push_back({ "workflow.displayName.required", "Workflow displayName is required." });
    }
    if (!isWorkflowSourceValid(workflow.source)) {
        issues.push_back({ "workflow.source.invalid", "Workflow source must be manual, imported, or starter-template." });
    }
    if (workflow.steps.empty()) {
        issues.push_back({ "workflow.steps.required", "Workflow must contain at least one step." });
    }
    for (const auto& step : workflow.steps) {
        if (step.stepId.empty()) {
            issues.push_back({ "workflow.stepId.required", "Every workflow step requires a stepId." });
        }
        if (!isWorkflowStepKindValid(step.kind)) {
            issues.push_back({ "workflow.stepKind.invalid", "Workflow step kind is not supported." });
        }
        if (step.target.empty()) {
            issues.push_back({ "workflow.stepTarget.required", "Every workflow step requires a target." });
        }
    }
    return issues;
}

inline bool isWorkflowReady(const WorkflowDefinition& workflow) {
    return workflow.enabled && validateWorkflowDefinition(workflow).empty();
}

inline WorkflowReadinessCounts workflowReadinessCounts(const std::vector<WorkflowDefinition>& workflows) {
    WorkflowReadinessCounts counts;
    for (const auto& workflow : workflows) {
        if (!workflow.enabled) {
            ++counts.disabled;
            continue;
        }
        if (validateWorkflowDefinition(workflow).empty()) {
            ++counts.ready;
        } else {
            ++counts.invalid;
        }
    }
    counts.missing = counts.ready > 0 ? counts.invalid : (std::max)(1, counts.invalid + counts.disabled);
    return counts;
}

} // namespace MasterControl
