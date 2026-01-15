#include "algo/plan_manager.h"
#include "data/pdt_node.h"
#include "data/htn_instance.h"
#include "util/names.h"
#include "util/log.h"
#include "util/command_utils.h"
#include "util/project_utils.h"

#include <vector>
#include <map>
#include <string>
#include <sstream>
#include <algorithm>
#include <set>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <cstdio>
#include <memory>
#include "util/temp_file.h"

int PlanManager::processNode(PdtNode *node, int &counter, const AbstractTask *parent_task, std::vector<std::pair<int, std::string>> &actions, std::vector<std::string> &abstract_tasks)
{

    Log::i("Processing node %s\n", TOSTR(*node));

    const auto &op_solution = node->getOpSolution();
    int op_id = op_solution.first;
    OpType op_type = op_solution.second;
    bool is_raw_action = false; // All actions which should be removed from the final plan (precondition_action and __noop actions (special action for methods without subtasks))

    // Assign the next available plan ID
    int current_plan_id = counter++;

    if (op_type == OpType::ACTION)
    {

        Log::i("Solution is action %s at ts:%d\n", TOSTR(_htn.getActionById(op_id)), node->getTsSolution());

        if (op_id == -1 || op_id == -2 || op_id == -3)
        {
            Log::i("Skipping special action %s\n", TOSTR(_htn.getActionById(op_id)));
            return -1; // Skip this action
        }

        // Replace the node with the leaf node
        while (node->getChildren().size() > 0)
        {
            node = node->getChildren()[0];
        }

        const Action &action = _htn.getActionById(op_id);
        int ts = _partial_order_problem ? node->getTsSolution() : actions.size();
        // If the action is a method precondition, we don't want to include it in the final plan
        // So if the stirng __method_precondition appears in the action name, we skip it
        std::string action_str = std::to_string(current_plan_id) + " " + TOSTR(action);
        actions.push_back({ts, action_str});
        if (action.getName().find("__method_precondition") != std::string::npos || action.getName() == "__noop")
        {
            is_raw_action = true; // Mark as precondition action
        }
    }
    else // op_type == OpType::METHOD
    {
        const Method &method = _htn.getMethodById(op_id);
        Log::i("Solution is method %s at ts:%d\n", TOSTR(method), node->getTsSolution());
        std::stringstream abstract_task_ss;
        if (_htn.isRootTask(*parent_task))
        {
            // We are the root node
            abstract_task_ss << "root " << current_plan_id << std::endl;
        }
        abstract_task_ss << current_plan_id << " " << parent_task->getName() << " -> " << TOSTR(method) << " ";

        std::vector<PdtNode *> &children = node->getChildren();
        const auto &subtasks = method.getSubtasksIdx(); // Indices of abstract tasks or actions in the method definition

        // if (_htn.getParams().isNonzero("removeMethodPrecAction") && _htn.methodContainsPreconditionAction(op_id)) {
        //     // Need to add a precondition action to the plan
        //     int action_precondition_id = _htn.getPreconditionActionId(op_id);
        //     const Action &action_precondition = _htn.getActionById(action_precondition_id);
        //     // This is a special action that is not part of the final plan
        //     int sub_op_plan_id = counter++;
        //     actions.push_back(std::to_string(sub_op_plan_id) + " " + action_precondition.getName());
        //     abstract_task_ss << " " << sub_op_plan_id;
            
        // }

        // Ordered children by their _ts_solution
        // if (_htn.getParams().isNonzero("po"))
        // {
        //     std::sort(children.begin(), children.end(), [](PdtNode *a, PdtNode *b)
        //               { return a->getTsSolution() < b->getTsSolution(); });
        // }

        // Iterate through the children of this position
        for (size_t j = 0; j < children.size(); ++j)
        {
            PdtNode *child_node = children[j];
            int idx = _htn.getParams().isNonzero("po") ? child_node->getParentMethodIdxToSubtaskIdx(op_id) : j; // Get the subtask index for the child node
            if (idx == -1)
            {
                continue; // Skip child position if it would contains only a blank action for the current method
            }
            int subtask_idx = subtasks[idx]; // Get the subtask index for the current child node

            // Recursively process the child node corresponding to the j-th subtask
            int sub_op_plan_id = -1; // Initialize to invalid
            if (_htn.isAbstractTask(subtask_idx))
            {
                // Pass the actual abstract task definition for the child's context
                const AbstractTask &subtask_definition = _htn.getAbstractTaskById(subtask_idx);
                sub_op_plan_id = processNode(child_node, counter, &subtask_definition, actions, abstract_tasks);
            }
            else // It's an action
            {
                // Pass nullptr as parent_task since the child represents an action directly
                sub_op_plan_id = processNode(child_node, counter, nullptr, actions, abstract_tasks);
            }

            // Only include subtasks that are part of the final plan (returned a valid plan ID >= 1)
            if (sub_op_plan_id != -1)
            {
                abstract_task_ss << " " << sub_op_plan_id;
            }
            // If sub_op_plan_id is -1, the branch for this subtask was not chosen or failed.
            // We don't include it in the method's decomposition list in the output.
        }
        abstract_tasks.push_back(abstract_task_ss.str());
    }

    // Return the plan ID assigned to the operation at this node (negative if it is a precondition action to indicate to remove it in the final plan)
    return is_raw_action ? -current_plan_id : current_plan_id;
}

std::string PlanManager::buildPlanRawString(std::vector<std::pair<int, std::string>> &actions, const std::vector<std::string> &abstract_tasks)
{
    std::stringstream stream;
    stream << "==>" << std::endl;

    // Sort actions based on timestamp (the first element of the pair)
    std::sort(actions.begin(), actions.end());

    // Output the collected actions in order
    for (const auto &action : actions)
    {
        stream << action.second << std::endl;
    }

    // Output the collected methods/abstract task decompositions in reverse order
    for (auto it = abstract_tasks.rbegin(); it != abstract_tasks.rend(); ++it)
    {
        stream << *it << std::endl;
    }

    stream << "<==" << std::endl;

    return stream.str();
}

std::string PlanManager::generateRawPlanString(PdtNode *root_node)
{
    if (!root_node)
    {
        Log::w("Warning: generateRawPlanString called with null root node.\n");
        return ""; // Return empty string on failure
    }
    // Data structures to hold the extracted plan components
    std::vector<std::pair<int, std::string>> actions; // Store pairs of (timestamp, action_string)
    std::vector<std::string> abstract_tasks;          // Store abstract task decompositions lines for the plan
    int counter = 1;                                  // Plan IDs start from 1

    // Start the recursive processing from the root node
    processNode(root_node, counter, &_htn.getRootTask(), actions, abstract_tasks);

    // Build and return the raw plan string
    return buildPlanRawString(actions, abstract_tasks);
}

std::optional<std::string> PlanManager::convertRawPlanToFinalPlan(const std::string &raw_plan_content)
{
    TempFile temp_raw_file;
    TempFile temp_final_file;

    if (temp_raw_file.path.empty() || temp_final_file.path.empty())
    {
        Log::e("Error: Failed to create temporary file names for conversion.\n");
        return std::nullopt;
    }

    // Write raw content to temp raw file
    {
        std::ofstream raw_out(temp_raw_file.path);
        if (!raw_out)
        {
            Log::e("Error: Failed to open temporary raw file '%s' for writing.\n", temp_raw_file.path.c_str());
            return std::nullopt;
        }
        raw_out << raw_plan_content;
    } // ofstream closes here

    // Run the converter command, redirecting stdout to the final temp file
    std::filesystem::path parser_path = getProjectRootDir() / "lib" / "pandaPIparser";
    // Note the change: output file path is now used for redirection '>' instead of being an argument
    std::string command = parser_path.string() + " --panda-converter " + temp_raw_file.path + " " + temp_final_file.path;

    Log::d("Running conversion command: %s\n", command.c_str());
    if (runCommand(command, "Failed to convert the raw plan to final format.") != 0)
    {
        Log::e("Error: Plan conversion command failed.\n");
        return std::nullopt; // Conversion failed
    }

    // Read the final content from the temp final file
    std::ifstream final_in(temp_final_file.path);
    if (!final_in)
    {
        Log::e("Error: Failed to open temporary final file '%s' for reading.\n", temp_final_file.path.c_str());
        return std::nullopt;
    }
    std::stringstream buffer;
    buffer << final_in.rdbuf();
    final_in.close(); // Close before temp file is potentially deleted by RAII

    return buffer.str(); // Return the final plan string
}

// Internal helper to run the verification process using a temporary file
bool PlanManager::runVerification(const std::string &final_plan_content)
{
    TempFile temp_verify_file; // RAII for cleanup
    if (temp_verify_file.path.empty())
    {
        Log::e("Error: Failed to create temporary file name for verification.\n");
        return false;
    }

    // Write final plan content to temp file
    {
        std::ofstream verify_out(temp_verify_file.path);
        if (!verify_out)
        {
            Log::e("Error: Failed to open temporary verification file '%s' for writing.\n", temp_verify_file.path.c_str());
            return false;
        }
        verify_out << final_plan_content;
    } // ofstream closes here

    // Run the verifier command
    std::filesystem::path parser_path = getProjectRootDir() / "lib" / "pandaPIparser";
    std::string command = parser_path.string() + " --verify " + _htn.getParams().getDomainFilename() + " " + _htn.getParams().getProblemFilename() + " " + temp_verify_file.path;

    Log::d("Running verification command: %s\n", command.c_str());
    if (runCommand(command, "Failed to verify the plan.") != 0)
    {
        Log::w("Plan verification failed for content.\n"); // It failed, but maybe not an error state for the caller
        return false;                                      // Verification failed
    }

    Log::i("Plan has been verified by pandaPIparser\n");
    return true; // Verification succeeded
}

// --- Public Methods ---

bool PlanManager::generatePlan(PdtNode *root_node)
{
    _final_plan_string = ""; // Reset internal state

    // 1. Generate raw plan string
    std::string raw_plan = generateRawPlanString(root_node);
    if (raw_plan.empty())
    {
        Log::e("Error: Failed to generate raw plan string representation.\n");
        return false;
    }

    Log::i("Raw plan generated:\n%s\n", raw_plan.c_str());

    // 2. Run conversion using temporary files
    std::optional<std::string> final_plan_opt = convertRawPlanToFinalPlan(raw_plan);

    if (!final_plan_opt)
    {
        Log::e("Error: Failed during plan conversion process.\n");
        return false; // Conversion failed
    }

    // 3. Store the final plan string internally
    _final_plan_string = *final_plan_opt;
    // Add the missing <== line
    _final_plan_string += "<==\n";

    // Compute the size of the plan
    std::istringstream iss(_final_plan_string);
    std::string line;
    int line_count = 0;
    while (std::getline(iss, line))
    {
        if (line == "==>")
            continue; // Skip the header

        // Stop when there is a root in the line
        if (line.find("root") != std::string::npos)
            break; // Stop at the root
        ++line_count;
    }
    _size_plan = line_count;

    return true; // Success
}

bool PlanManager::verifyPlan()
{
    if (_final_plan_string.empty())
    {
        Log::e("Error: Cannot verify plan. generatePlan() must be called successfully first.\n");
        return false;
    }
    return runVerification(_final_plan_string);
}

bool PlanManager::outputPlan(const std::string &filename) const
{
    if (_final_plan_string.empty())
    {
        Log::e("Error: Cannot output plan. generatePlan() must be called successfully first.\n");
        return false;
    }

    std::ofstream plan_file(filename);
    if (!plan_file.is_open())
    {
        Log::e("Error: Unable to open file '%s' for writing final plan.\n", filename.c_str());
        return false;
    }

    plan_file << _final_plan_string;
    plan_file.close();
    Log::i("Final plan written to: %s\n", filename.c_str());
    return true;
}

void PlanManager::outputPlan(std::ostream &os) const
{
    if (_final_plan_string.empty())
    {
        // Log::w might be better if printing an empty string isn't critical
        Log::e("Warning: Attempting to output an empty or ungenerated plan string.\n");
    }
    os << _final_plan_string;
}

const std::string &PlanManager::getPlanString() const
{
    // No check needed here, just return the current state (might be empty)
    return _final_plan_string;
}

const int PlanManager::getPlanSize() const
{
    return _size_plan;
}
