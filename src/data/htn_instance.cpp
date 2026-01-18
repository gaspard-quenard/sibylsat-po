#include "data/htn_instance.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <queue>
#include <assert.h>
#include <unordered_map>
#include <queue>
#include <map>           // Added for std::map
#include <vector>        // Added for std::vector (though likely included already)
#include <algorithm>     // Added for std::max, std::min
#include <unordered_set> // Added for std::unordered_set in calculateStrictlyBefore
#include <algorithm>     // Added for std::sort in calculateStrictlyBefore

#include "util/log.h"
#include "util/command_utils.h"
#include "util/project_utils.h"
#include "util/names.h"
#include "sat/variable_provider.h"
#include "algo/effects_inference.h"

HtnInstance::HtnInstance(Parameters &params) : _params(params), _stats(Statistics::getInstance())
{
    Log::i("Parsing the domain and problem files...\n");
    auto parsed_problem = parseProblem(params.getDomainFilename(), params.getProblemFilename());
    if (!parsed_problem)
        return;

    Log::i("Grounding the parsed problem...\n");
    auto grounded_problem = groundProblem(*parsed_problem);
    if (!grounded_problem)
        return;

    loadGroundedProblem(*grounded_problem);

    if (_params.isNonzero("sibylsat"))
    {
        if (!_partial_order_problem)
        {
            bool res = getPrecsAndEffsMethods(*grounded_problem);
            exit(0);
        }
        else
        {
            EffectsInference effects_calculator(*this);
            _stats.beginTiming(TimingStage::COMPUTE_PRECS_AND_EFFS);
            effects_calculator.calculateAllMethodsPrecsAndEffs(_methods, &_mutex);
            // effects_calculator.printAllMethodPrecsAndEffs();
            // exit(0);
            _stats.endTiming(TimingStage::COMPUTE_PRECS_AND_EFFS);
        }
    }
}

std::optional<std::string> HtnInstance::parseProblem(const std::string &domain_filepath, const std::string &problem_filepath)
{
    std::filesystem::path parser_path = getProjectRootDir() / "lib" / "pandaPIparser";
    std::string output_filepath = (getProblemProcessingDir() / "problem.parsed").string();
    // std::string command = parser_path.string() + " " + domain_filepath + " " + problem_filepath + " " + output_filepath;
    std::string options = "";
    if (_params.isNonzero("nsp"))
    {
        options += "--no-split-parameters";
    }
    std::string command = parser_path.string() + " " + options + " " + domain_filepath + " " + problem_filepath + " " + output_filepath;
    // std::string command = parser_path.string() + " --no-split-parameters " + domain_filepath + " " + problem_filepath + " " + output_filepath;

    if (runCommand(command, "Parsing the domain and problem files failed.") != 0)
        return std::nullopt;

    return output_filepath;
}

std::optional<std::string> HtnInstance::groundProblem(const std::string &parsed_problem_filepath)
{
    if (!std::filesystem::exists(parsed_problem_filepath))
    {
        Log::e("Error: The parsed problem file does not exist.\n");
        return std::nullopt;
    }

    std::filesystem::path grounder_path = getProjectRootDir() / "lib" / "pandaPIgrounder";
    std::string output_filepath = (getProblemProcessingDir() / "problem.grounded").string();
    std::string options = "";
    if (_params.isNonzero("mutex"))
    {
        options += "--invariants";
    }
    std::string command = grounder_path.string() + " " + options + " " + parsed_problem_filepath + " " + output_filepath;

    if (runCommand(command, "Grounding the parsed problem failed.") != 0)
        return std::nullopt;

    return output_filepath;
}

void HtnInstance::loadGroundedProblem(const std::string &grounded_problem_filepath)
{
    if (!std::filesystem::exists(grounded_problem_filepath))
    {
        Log::e("Error: The grounded problem file does not exist.\n");
        return;
    }

    std::ifstream file(grounded_problem_filepath);
    if (!file)
    {
        Log::e("Error: Unable to open the grounded problem file.\n");
        return;
    }

    int line_idx = 0;
    extractPredicates(file, line_idx);
    if (_params.isNonzero("mutex"))
    {
        extractMutexes(file, line_idx);
    }
    extractActions(file, line_idx);
    extractInitGoalStates(file, line_idx);
    extractTasksNames(file, line_idx);
    extractInitRootTaskIdx(file, line_idx);
    extractMethods(file, line_idx);

    // Initialize the blank action
    _blankAction = new Action(_id_blank_action, {}, {}, {});
    _blankAction->addName("blank");

    // Initialize the init and goal action (only used in partial order with before)
    // _id_init_action = _actions.size() + _abstr_tasks.size();
    // _id_goal_action = _actions.size() + _abstr_tasks.size() + 1;
    std::vector<int> init_pos_effects;
    std::vector<int> init_neg_effects;
    for (int i = 0; i < _predicates.size(); i++)
    {
        if (_init_state.find(i) != _init_state.end())
        {
            init_pos_effects.push_back(i);
        }
        else
        {
            init_neg_effects.push_back(i);
        }
    }
    _init_action = new Action(_id_init_action, {}, init_pos_effects, init_neg_effects);
    _init_action->addName("__init__");

    std::vector<int> goal_pos_precs;
    for (int i = 0; i < _predicates.size(); i++)
    {
        if (_goal_state.find(i) != _goal_state.end())
        {
            goal_pos_precs.push_back(i);
        }
    }
    _goal_action = new Action(_id_goal_action, goal_pos_precs, {}, {});
    _goal_action->addName("__goal__");

    // Initialize all the facts vars for the goal state
    for (int i = 0; i < _predicates.size(); i++)
    {
        _all_fact_vars_goal.push_back(VariableProvider::nextVar());
    }

    if (_params.isNonzero("removeMethodPrecAction"))
    {
        Log::i("Removing the first subtask of each method if it starts with __method_precondition_\n");
        // Iterate over all the method and remove the first subtask if it starts with __method_prec__
        for (int i = 0; i < _methods.size(); i++)
        {
            Method &method = _methods[i];
            // Log::i("Method %s\n", TOSTR(method));
            if (method.getSubtasksIdx().size() > 0)
            {
                // for (int j = 0; j < method.getSubtasksIdx().size(); j++)
                // {
                //     int subtask_id = method.getSubtasksIdx()[j];
                //     if (!isAbstractTask(subtask_id) &&
                //         _actions[subtask_id].getName().find("__method_precondition_") == 0)
                //     {
                //         Log::i("PRECONDITION %s\n", TOSTR(_actions[subtask_id]));
                //     }
                // }

                int first_subtask_id = method.getSubtasksIdx()[0];
                if (!isAbstractTask(first_subtask_id) &&
                    _actions[first_subtask_id].getName().find("__method_precondition_") == 0)
                {
                    Log::i("Removing the first subtask %s of %s\n", TOSTR(_actions[first_subtask_id]), TOSTR(method));
                    // Add all preconditions of the first subtask to the method to the preconditions of the method
                    const std::vector<int> &preconditions = _actions[first_subtask_id].getPreconditionsIdx();
                    for (int prec : preconditions)
                    {
                        method.addPreconditionIdx(prec);
                    }

                    _methods_to_precondition_action[i] = first_subtask_id;
                    // Remove the first subtask
                    method.removeFirstSubtask();
                }
            }
        }
    }

    if (_partial_order_problem) {
        addInitAndGoalActionsToRootMethod();
    }
    

    for (const auto& method: _methods) {

        int method_id = method.getId();
        const std::vector<int> &subtasks_ids = method.getSubtasksIdx();
        const std::vector<std::pair<int, int>> &ordering_constains = method.getOrderingConstraints();
            // TEST
        // Have a specific structure to know all the methods which have the same number of subtasks and the same ordering between them
        std::vector<std::pair<int, int>> canonical_ordering_constraints = ordering_constains;
        std::sort(canonical_ordering_constraints.begin(), canonical_ordering_constraints.end());

        std::pair<int, std::vector<std::pair<int, int>>> current_structure_key = 
            std::make_pair(static_cast<int>(subtasks_ids.size()), canonical_ordering_constraints);

        int structure_id;
        auto it = _canonical_structure_to_id.find(current_structure_key);
        if (it == _canonical_structure_to_id.end())
        {
            structure_id = _next_structure_id++;
            _canonical_structure_to_id[current_structure_key] = structure_id;
            // Store num_subtasks and canonical_ordering_constraints in _structure_id_to_details
            _structure_id_to_details[structure_id] = std::make_pair(static_cast<int>(subtasks_ids.size()), canonical_ordering_constraints);
        }
        else
        {
            structure_id = it->second;
        }
        _method_to_structure_id[method_id] = structure_id;
    }

    // Some preprocessing, indicate all the methods which have the same number of subtasks and the same ordering between them

    Names::init(_predicates, _actions, _abstr_tasks, _methods, _blankAction, _init_action, _goal_action);

    // To debug, print all structures
    // Log::i("Predicates:\n");
    // for (const Predicate &pred : _predicates)
    // {
    //     Log::i("%s\n", TOSTR(pred));
    // }
    // Log::i("Actions:\n");
    // for (const Action &act : _actions)
    // {
    //     Log::i("(%d) %s\n", act.getId(), TOSTR(act));
    //     // Print preconditions and effets
    //     Log::i("  Preconditions:\n");
    //     for (const int pred_id : act.getPreconditionsIdx())
    //     {
    //         Log::i("    %s\n", TOSTR(_predicates[pred_id]));
    //     }
    //     Log::i("  Positive effects:\n");
    //     for (const int pred_id : act.getPosEffsIdx())
    //     {
    //         Log::i("    %s\n", TOSTR(_predicates[pred_id]));
    //     }
    //     Log::i("  Negative effects:\n");
    //     for (const int pred_id : act.getNegEffsIdx())
    //     {
    //         Log::i("    - %s\n", TOSTR(_predicates[pred_id]));
    //     }
    // }
    // Log::i("Abstract tasks:\n");
    // for (const AbstractTask &task : _abstr_tasks)
    // {
    //     Log::i("(%d) %s\n", task.getId(), TOSTR(task));
    //     Log::i("  Can be decomposed by:\n");
    //     for (const int method_idx : task.getDecompositionMethodsIdx())
    //     {
    //         Log::i("    (%d) %s\n", method_idx, TOSTR(_methods[method_idx]));
    //     }
    // }
    // Log::i("Methods:\n");
    // for (const Method &method : _methods)
    // {
        // Log::i("(%d) %s\n", method.getId(), TOSTR(method));
        // Print all subtasks
        // Log::i("  Subtasks (num: %d):\n", method.getSubtasksIdx().size());
        // for (const int subtask_id : method.getSubtasksIdx())
        // {
        //     if (subtask_id < _actions.size())
        //     {
        //         Log::i("    (%d) %s\n", subtask_id, TOSTR(_actions[subtask_id]));
        //     }
        //     else
        //     {
        //         Log::i("    (%d) %s\n", subtask_id, TOSTR(_abstr_tasks[subtask_id - _actions.size()]));
        //     }
        // }
    // }
    // Log::i("Initial state:\n");
    // for (int pred_id : _init_state)
    // {
    //     Log::i("%s\n", TOSTR(_predicates[pred_id]));
    // }
    // Log::i("Goal state:\n");
    // for (int pred_id : _goal_state)
    // {
    //     Log::i("%s\n", TOSTR(_predicates[pred_id]));
    // }
}

void HtnInstance::skipUntil(std::ifstream &file, const std::string &target, int &line_idx)
{
    std::string line;
    while (std::getline(file, line))
    {
        ++line_idx;
        if (line == target)
            return;
    }
    // Indicate that the target was not found
    Log::e("Error: Target string '%s' not found in the file.\n", target.c_str());
}

std::vector<int> HtnInstance::parseIntegerList(std::ifstream &file, int &line_idx)
{
    std::vector<int> numbers;
    std::string line;
    std::getline(file, line);
    std::istringstream iss(line);
    int value;
    while (iss >> value)
    {
        if (value == -1)
            break;
        numbers.push_back(value);
    }
    ++line_idx;
    return numbers;
}

void HtnInstance::extractPredicates(std::ifstream &file, int &line_idx)
{
    skipUntil(file, ";; #state features", line_idx);

    std::string line;
    std::getline(file, line);
    int num_predicates = std::stoi(line);
    _predicates.reserve(num_predicates);
    ++line_idx;

    int pred_id = 0;
    while (std::getline(file, line) && !line.empty())
    {
        bool is_positive = line[0] == '+';
        _predicates.emplace_back(pred_id++, is_positive, line);
        ++line_idx;
    }
}

void HtnInstance::extractMutexes(std::ifstream &file, int &line_idx)
{
    skipUntil(file, ";; Mutex Groups", line_idx);

    std::string line;

    // Ignore the first line
    std::getline(file, line);
    ++line_idx;

    // Each mutex group is in the form:
    // <idx_first_predicate_in_group> <idx_last_predicate_in_group> <name>
    while (std::getline(file, line) && !line.empty())
    {
        std::istringstream iss(line);
        int idx_first_predicate, idx_last_predicate;
        iss >> idx_first_predicate >> idx_last_predicate;
        if (idx_first_predicate == idx_last_predicate)
        {
            continue; // Skip empty mutex groups
        }
        std::vector<int> mutex_group;
        mutex_group.reserve(idx_last_predicate - idx_first_predicate + 1);
        for (int i = idx_first_predicate; i <= idx_last_predicate; ++i)
        {
            mutex_group.push_back(i);
        }
        _mutex.addMutexGroup(mutex_group);
        ++line_idx;
    }

    Log::i("Line: %s\n", line.c_str());

    skipUntil(file, ";; further strict Mutex Groups", line_idx);

    // Ignore the first line
    std::getline(file, line);
    ++line_idx;

    // Here we have a list if integers, each integer is a mutex group
    std::vector<int> mutex_group = parseIntegerList(file, line_idx);
    while (mutex_group.size() > 1)
    {
        // std::unordered_set<int> mutex_group_set;
        // for (int i = 0; i < mutex_group.size(); ++i)
        // {
        //     mutex_group_set.insert(mutex_group[i]);
        // }
        // _mutex.addMutexGroup(mutex_group_set);
        _mutex.addMutexGroup(mutex_group);
        mutex_group = parseIntegerList(file, line_idx);
    }

    skipUntil(file, ";; further non strict Mutex Groups", line_idx);
    // Ignore the first line
    std::getline(file, line);
    ++line_idx;

    mutex_group = parseIntegerList(file, line_idx);
    while (mutex_group.size() > 1)
    {
        // std::unordered_set<int> mutex_group_set;
        // for (int i = 0; i < mutex_group.size(); ++i)
        // {
        //     mutex_group_set.insert(mutex_group[i]);
        // }
        // _mutex.addMutexGroup(mutex_group_set);
        _mutex.addMutexGroup(mutex_group);
        mutex_group = parseIntegerList(file, line_idx);
    }
}

void HtnInstance::extractActions(std::ifstream &file, int &line_idx)
{
    skipUntil(file, ";; Actions", line_idx);

    std::string line;
    std::getline(file, line);
    int num_actions = std::stoi(line);
    _actions.reserve(num_actions);
    ++line_idx;

    /**
     * Each block of $4$ lines describes an action.
     *
     * The first line of each action description contains a single integer -- the cost of that action.
       The second line contains a space-separated list of integers.
       The last integer is always $-1$ and all other integers are non-negative.
       Each such non-negative integer $i$ describes that the state feature $i$ is a precondition of this action.
       The third and fourth line per action have the same format.
       They consist of a sequence of blocks of integers.
       Each block starts with an integer $\ell$.
       The line ends with block with $\ell = -1$ which carries no further semantics.
       For each other block, $\ell+1$ non-negative integers follow after the value $\ell$.
       Each such block describes a conditional effect of the action.
       The first $\ell$ integers after the first integer $\ell$ of each block describe the conditions of this conditional effect.
       Each such integer $i$ refers to the state feature $i$.
       The last integer $e$ of each block describes the effect of the conditional effect.
       In the third line of each action this is the effect of adding the state feature $e$, while in the fourth line, it is the effect of deleting the state feature $e$.
       Note that there might be *two* space between the last integer of each block and the next start of a block.
     */

    int action_id = 0;
    while (std::getline(file, line) && !line.empty())
    {
        int cost = std::stoi(line);
        ++line_idx;

        std::vector<int> preconditions = parseIntegerList(file, line_idx);
        std::vector<int> positive_conditional_and_unconditional_effects = parseIntegerList(file, line_idx);
        std::vector<int> negative_conditional_and_unconditional_effects = parseIntegerList(file, line_idx);

        // Check that all positive and negative effects are unconditional (i.e., no conditional effects)
        // To do so, all the even idx elements of the effects list should be 0
        // and all the odd idx elements should be the predicate ID
        std::vector<int> positive_effects, negative_effects;
        bool even = true;
        for (int effect : positive_conditional_and_unconditional_effects)
        {
            if (even)
            {
                if (effect != 0)
                {
                    Log::e("Error: Conditional effects are not supported.\n");
                    return;
                }
            }
            else
            {
                positive_effects.push_back(effect);
            }
            even = !even;
        }
        even = true;

        for (int effect : negative_conditional_and_unconditional_effects)
        {
            if (even)
            {
                if (effect != 0)
                {
                    Log::e("Error: Conditional effects are not supported.\n");
                    return;
                }
            }
            else
            {
                negative_effects.push_back(effect);
            }
            even = !even;
        }

        _actions.emplace_back(action_id++, preconditions, positive_effects, negative_effects);
    }
    Log::i("There are %d actions in the grounded problem.\n", num_actions);
}

void HtnInstance::extractInitGoalStates(std::ifstream &file, int &line_idx)
{

    /**
     * Each such non-negative integer $i$ indicates that the state feature $i$ holds in the initial state.
       Any state feature that does not occur in this line is false in the initial state
     */
    skipUntil(file, ";; initial state", line_idx);
    std::vector<int> init_state = parseIntegerList(file, line_idx);
    // Convert the initial state to a unordered set
    _init_state = std::unordered_set<int>(init_state.begin(), init_state.end());

    skipUntil(file, ";; goal", line_idx);
    std::vector<int> goal_state = parseIntegerList(file, line_idx);
    // Convert the goal state to a unordered set
    _goal_state = std::unordered_set<int>(goal_state.begin(), goal_state.end());
}

void HtnInstance::extractTasksNames(std::ifstream &file, int &line_idx)
{
    skipUntil(file, ";; tasks (primitive and abstract)", line_idx);

    std::string line;
    std::getline(file, line);
    int num_tasks = std::stoi(line);
    _abstr_tasks.reserve(num_tasks - _actions.size());
    ++line_idx;

    int task_id = 0;
    while (std::getline(file, line) && !line.empty())
    {
        bool is_abstract = line[0] == '1';
        std::string task_name = line.substr(2);

        if (is_abstract)
        {
            _abstr_tasks.emplace_back(task_id, task_name);
        }
        else
        {
            _actions[task_id].addName(task_name);
        }
        ++line_idx;
        ++task_id;
    }
}

void HtnInstance::extractInitRootTaskIdx(std::ifstream &file, int &line_idx)
{
    skipUntil(file, ";; initial abstract task", line_idx);

    std::string line;
    std::getline(file, line);
    _root_task_idx = std::stoi(line);
    ++line_idx;
}

void HtnInstance::extractMethods(std::ifstream &file, int &line_idx)
{
    skipUntil(file, ";; methods", line_idx);

    std::string line;
    std::getline(file, line);
    int num_methods = std::stoi(line);
    _methods.reserve(num_methods);
    ++line_idx;

    /**
     * Each method is described by a block of $4$ consecutive lines.
     *
     * The first line per decomposition method always contains a string without any whitespace character -- the name of the method.
       The second line contains a single non-negative integer $a$ -- the abstract task this method decomposes.
       The third line contains a space-separated list of integers.
       The last integer is always $-1$, while all other integers are non-negative.
       The $i$th (0-indexed) of these integers describes the $i$th subtask of this method.
       The value of the $i$th integer indicates the task of this subtask.
       The fourth line contains an odd number of integers.
       The last integer is always $-1$, while the other integers are non-negative.
       The number of non-negative integers is thus always even.
       Hence this list of non-negative integers can be interpreted as sequence of pairs $\langle (o_1^-,o_1^+), \dots, (o_n^-,o_n^+) \rangle$.
       Each such pair $(o_i^-,o_i^+)$ describes an ordering constraint on the methods subtasks.
       More precisely, each such pair stipulates that the $o_i^-$th subtask must occur before the $o_i^+$th subtask.
       No guarantees are made w.r.t. to the question whether the set of ordering constraints is transitively closed or whether it contains
       contains transitively implied ordering.
     */

    int method_id = 0;
    while (std::getline(file, line) && !line.empty())
    {
        std::string method_name = line;
        ++line_idx;

        std::vector<int> abstract_task_ids = parseIntegerList(file, line_idx);

        if (abstract_task_ids.empty())
        {
            Log::e("Error: No abstract task ID found for method %s\n", method_name.c_str());
            return;
        }
        if (abstract_task_ids.size() > 1)
        {
            Log::e("Error: Multiple abstract task IDs found for method %s\n", method_name.c_str());
            return;
        }

        int abstract_task_id = abstract_task_ids[0];
        std::vector<int> subtasks_ids = parseIntegerList(file, line_idx);

        std::vector<std::pair<int, int>> ordering_constains;
        std::getline(file, line);
        std::istringstream iss(line);
        int value1, value2;
        while (iss >> value1)
        {
            if (value1 == -1)
                break;
            iss >> value2;
            ordering_constains.emplace_back(value1, value2);
        }
        ++line_idx;

        if (!_partial_order_problem)
        {
            sortSubtasks(subtasks_ids, ordering_constains);
        }

        // Pass both maps to the constructor
        _methods.emplace_back(method_id, method_name, abstract_task_id, subtasks_ids, ordering_constains);

        // Add the method to the abstract task
        _abstr_tasks[abstract_task_id - _actions.size()].addDecompositionMethod(method_id);
                
        ++method_id;
    }
}

void HtnInstance::sortSubtasks(std::vector<int> &subtasks_id, std::vector<std::pair<int, int>> &ordering_constraints)
{
    int n = subtasks_id.size();
    // Build a graph using indices as nodes.
    std::vector<std::vector<int>> graph(n);
    std::vector<int> indegree(n, 0);

    // Build the graph based on ordering constraints (which are indices).
    for (const auto &constraint : ordering_constraints)
    {
        int u = constraint.first;  // index of the first subtask
        int v = constraint.second; // index of the second subtask

        // Check that indices are in range.
        if (u < 0 || u >= n || v < 0 || v >= n)
        {
            throw std::runtime_error("Ordering constraint index out of range");
        }
        graph[u].push_back(v);
        indegree[v]++;
    }

    // Use a queue for the topological sort.
    std::queue<int> q;
    for (int i = 0; i < n; i++)
    {
        if (indegree[i] == 0)
            q.push(i);
    }

    // Kahn's algorithm for topological sorting.
    std::vector<int> sortedIndices;
    while (!q.empty())
    {
        int u = q.front();
        q.pop();
        sortedIndices.push_back(u);
        for (int v : graph[u])
        {
            if (--indegree[v] == 0)
                q.push(v);
        }
    }

    if (sortedIndices.size() != n)
    {
        throw std::runtime_error("Cycle detected in ordering constraints");
    }

    // Build the sorted subtasks list according to the topologically sorted indices.
    std::vector<int> sortedSubtasks(n);
    for (int i = 0; i < n; i++)
    {
        sortedSubtasks[i] = subtasks_id[sortedIndices[i]];
    }
    subtasks_id = sortedSubtasks;
}

bool HtnInstance::getPrecsAndEffsMethods(std::string &grounded_problem_filepath)
{
    std::filesystem::path planner_path = getProjectRootDir() / "lib" / "pandaPIengine";
    std::string output_filepath = (getProblemProcessingDir() / "precs_effs_methods.txt").string();

    std::string options = "--writePrecsAndEffsMethods=" + output_filepath;
    std::string command = planner_path.string() + " " + options + " " + grounded_problem_filepath;

    if (runCommand(command, "Getting preconditions and effects of methods failed.") != 0)
        return false;

    // Read the output file and extract the preconditions and effects
    Log::i("Reading the preconditions and effects of methods from %s\n", output_filepath.c_str());
    std::ifstream file(output_filepath);
    if (!file)
    {
        Log::e("Error: Unable to open the preconditions and effects file.\n");
        return false;
    }
    std::string line;
    // Format of the file is in the form:
    // Each method is described by a block of $6$ consecutive lines.
    // The first line contains the method ID.
    // The next fifth lines contain a space-separated list of integers finishing with -1.
    // The second line contains the preconditions of the method.
    // The third line contains the possible positive effects of the method.
    // The fourth line contains the possible negative effects of the method.
    // The fifth line contains the positive effects of the method.
    // The sixth line contains the negative effects of the method.

    int line_idx = 0;
    while (std::getline(file, line) && !line.empty())
    {
        int method_id = std::stoi(line);
        std::vector<int> preconditions = parseIntegerList(file, line_idx);
        std::vector<int> possible_positive_effects = parseIntegerList(file, line_idx);
        std::vector<int> possible_negative_effects = parseIntegerList(file, line_idx);
        std::vector<int> positive_effects = parseIntegerList(file, line_idx);
        std::vector<int> negative_effects = parseIntegerList(file, line_idx);

        std::unordered_set<int> preconditions_set(preconditions.begin(), preconditions.end());
        std::unordered_set<int> possible_positive_effects_set(possible_positive_effects.begin(), possible_positive_effects.end());
        std::unordered_set<int> possible_negative_effects_set(possible_negative_effects.begin(), possible_negative_effects.end());
        std::unordered_set<int> positive_effects_set(positive_effects.begin(), positive_effects.end());
        std::unordered_set<int> negative_effects_set(negative_effects.begin(), negative_effects.end());

        // Set the preconditions and effects of the method
        _methods[method_id].setPreconditions(preconditions_set);
        _methods[method_id].setPossiblePositiveEffects(possible_positive_effects_set);
        _methods[method_id].setPossibleNegativeEffects(possible_negative_effects_set);
        _methods[method_id].setPositiveEffects(positive_effects_set);
        _methods[method_id].setNegativeEffects(negative_effects_set);

        // Debug print
        Log::i("For method %s (id: %d):\n", TOSTR(_methods[method_id]), method_id);
        Log::i("Preconditions:\n");
        for (int precondition : preconditions)
        {
            Log::i("  %s\n", TOSTR(_predicates[precondition]));
        }
        Log::i("Possible positive effects:\n");
        for (int effect : possible_positive_effects)
        {
            Log::i("  %s\n", TOSTR(_predicates[effect]));
        }
        Log::i("Possible negative effects:\n");
        for (int effect : possible_negative_effects)
        {
            Log::i("  %s\n", TOSTR(_predicates[effect]));
        }
        Log::i("Positive effects:\n");
        for (int effect : positive_effects)
        {
            Log::i("  %s\n", TOSTR(_predicates[effect]));
        }
        Log::i("Negative effects:\n");
        for (int effect : negative_effects)
        {
            Log::i("  %s\n", TOSTR(_predicates[effect]));
        }
        Log::i("\n");
    }

    exit(0);

    return true;
}

// Implementation of the new static function
std::unordered_map<int, std::vector<int>> HtnInstance::calculateSubtaskTimeSteps(
    const std::vector<int> &subtasks_ids,
    const std::vector<std::pair<int, int>> &ordering_constraints,
    const std::string &method_name)
{
    std::unordered_map<int, std::vector<int>> subtask_possible_timesteps;
    int n = subtasks_ids.size();

    if (n == 0)
    {
        return subtask_possible_timesteps; // Return empty map if no subtasks
    }

    std::vector<std::vector<int>> adj(n), rev_adj(n);
    std::vector<int> in_degree(n, 0), out_degree(n, 0);

    // Build graph
    for (const auto &constraint : ordering_constraints)
    {
        int u = constraint.first;
        int v = constraint.second;
        if (u < 0 || u >= n || v < 0 || v >= n)
        {
            Log::e("Error: Ordering constraint index out of range for method %s\n", method_name.c_str());
            return {}; // Return empty map on error
        }
        adj[u].push_back(v);
        rev_adj[v].push_back(u);
        in_degree[v]++;
        out_degree[u]++;
    }

    // Calculate Earliest Start Time (EST)
    std::vector<int> est(n, 0);
    std::queue<int> q_est;
    std::vector<int> current_in_degree = in_degree; // Use a copy for EST calculation
    for (int i = 0; i < n; ++i)
    {
        if (current_in_degree[i] == 0)
        {
            q_est.push(i);
        }
    }

    int processed_count_est = 0;
    while (!q_est.empty())
    {
        int u = q_est.front();
        q_est.pop();
        processed_count_est++;

        for (int v : adj[u])
        {
            est[v] = std::max(est[v], est[u] + 1);
            if (--current_in_degree[v] == 0)
            {
                q_est.push(v);
            }
        }
    }

    if (processed_count_est != n)
    {
        Log::e("Error: Cycle detected in ordering constraints for method %s (EST calculation)\n", method_name.c_str());
        return {}; // Return empty map if cycle detected
    }

    // Calculate Latest Finish Time (LFT) - finish time is inclusive
    std::vector<int> lft(n, n - 1);
    std::queue<int> q_lft;
    std::vector<int> current_out_degree = out_degree; // Use a copy for LFT calculation
    for (int i = 0; i < n; ++i)
    {
        if (current_out_degree[i] == 0)
        {
            q_lft.push(i);
        }
    }

    int processed_count_lft = 0;
    while (!q_lft.empty())
    {
        int v = q_lft.front();
        q_lft.pop();
        processed_count_lft++;

        for (int u : rev_adj[v])
        {
            lft[u] = std::min(lft[u], lft[v] - 1);
            if (--current_out_degree[u] == 0)
            {
                q_lft.push(u);
            }
        }
    }

    if (processed_count_lft != n)
    {
        // This case should ideally not happen if EST calculation passed without detecting a cycle.
        Log::e("Error: Cycle detected or graph issue in ordering constraints for method %s (LFT calculation)\n", method_name.c_str());
        return {}; // Return empty map if cycle detected
    }

    // Populate the result map
    Log::i("Method %s (%d subtasks) - Time Step Calculation:\n", method_name.c_str(), n);
    for (int i = 0; i < n; ++i)
    {
        std::vector<int> possible_steps;
        if (est[i] <= lft[i])
        {
            for (int t = est[i]; t <= lft[i]; ++t)
            {
                possible_steps.push_back(t);
            }
        }
        else
        {
            Log::w("Warning: For method %s, subtask index %d has EST (%d) > LFT (%d). Possible issue or cycle.\n", method_name.c_str(), i, est[i], lft[i]);
            // Decide how to handle this: maybe add no steps, or log more verbosely.
            // For now, it results in an empty possible_steps vector for this subtask.
        }
        subtask_possible_timesteps[i] = possible_steps; // Store using subtask index 'i'

        // Log output
        std::string steps_str = "[";
        if (!possible_steps.empty())
        {
            for (size_t j = 0; j < possible_steps.size(); ++j)
            {
                steps_str += std::to_string(possible_steps[j]) + (j == possible_steps.size() - 1 ? "" : ", ");
            }
        }
        steps_str += "]";
        Log::i("  Subtask Index %d (ID: %d): EST=%d, LFT=%d, Possible Steps: %s\n", i, subtasks_ids[i], est[i], lft[i], steps_str.c_str());
    }

    return subtask_possible_timesteps;
}

// Function to calculate strictly preceding subtasks for each subtask
std::unordered_map<int, std::vector<int>> HtnInstance::calculateDirectPredecessors(
    int num_subtasks,
    const std::vector<std::pair<int, int>> &ordering_constraints)
{
    std::unordered_map<int, std::vector<int>> strictly_before_map;
    if (num_subtasks == 0)
    {
        return strictly_before_map;
    }

    // Build the reversed adjacency list (edge v->u means u must come before v)
    std::vector<std::vector<int>> rev_adj(num_subtasks);
    for (const auto &constraint : ordering_constraints)
    {
        int u = constraint.first; // u must come before v
        int v = constraint.second;
        // Basic validation
        if (u >= 0 && u < num_subtasks && v >= 0 && v < num_subtasks)
        {
            if (u == v)
            {
                Log::w("Warning: Self-loop detected in ordering constraint for subtask %d in strictly_before calculation.\n", u);
                continue; // Skip self-loops as they don't contribute to strict precedence
            }
            rev_adj[v].push_back(u); // Add edge u to list of direct predecessors of v
        }
        else
        {
            Log::e("Error: Ordering constraint index out of range (%d or %d) for %d subtasks during strictly before calculation.\n", u, v, num_subtasks);
            return {}; // Return empty on error
        }
    }

    // For each subtask index 'i', find its direct predecessors from the reversed graph
    for (int i = 0; i < num_subtasks; ++i)
    {
        // Get the direct predecessors from the reversed adjacency list
        std::vector<int> direct_predecessors = rev_adj[i];

        // Sort predecessors for consistent output/debugging (optional)
        std::sort(direct_predecessors.begin(), direct_predecessors.end());
        strictly_before_map[i] = direct_predecessors; // Assign the list of direct predecessors

        // --- Optional Debug Logging ---
        std::string preds_str = "[";
        if (!direct_predecessors.empty())
        { // Changed 'predecessors' to 'direct_predecessors'
            for (size_t j = 0; j < direct_predecessors.size(); ++j)
            {                                                                                                            // Changed 'predecessors' to 'direct_predecessors'
                preds_str += std::to_string(direct_predecessors[j]) + (j == direct_predecessors.size() - 1 ? "" : ", "); // Changed 'predecessors' to 'direct_predecessors'
            }
        }
        preds_str += "]";
        Log::i("  Strictly Before Subtask Index %d: %s\n", i, preds_str.c_str());
        // --- End Optional Debug Logging ---
    }

    return strictly_before_map;
}

Parameters &HtnInstance::getParams() const
{
    return _params;
}

const Action &HtnInstance::getBlankAction() const
{
    return *_blankAction;
}

const bool HtnInstance::isAbstractTask(int task_id) const
{
    return task_id >= static_cast<int>(_actions.size());
}

const AbstractTask &HtnInstance::getAbstractTaskById(int task_id) const
{
    assert(task_id >= _actions.size() && task_id < _abstr_tasks.size() + _actions.size() || Log::e("Error: Task ID %d is out of range.\n", task_id));
    return _abstr_tasks[task_id - _actions.size()];
}

const Action &HtnInstance::getActionById(int action_id) const
{
    if (action_id == _blankAction->getId())
    {
        return getBlankAction();
    }
    if (action_id == _init_action->getId())
    {
        return *_init_action;
    }
    if (action_id == _goal_action->getId())
    {
        return *_goal_action;
    }
    assert(action_id >= 0 && action_id < _actions.size() || Log::e("Error: Action ID %d is out of range.\n", action_id));
    return _actions[action_id];
}

const Method &HtnInstance::getMethodById(int method_id) const
{
    assert(method_id >= 0 && method_id < _methods.size() || Log::e("Error: Method ID %d is out of range.\n", method_id));
    return _methods[method_id];
}

const Predicate &HtnInstance::getPredicateById(int pred_id) const
{
    assert(pred_id >= 0 && pred_id < _predicates.size() || Log::e("Error: Predicate ID %d is out of range.\n", pred_id));
    return _predicates[pred_id];
}

const AbstractTask &HtnInstance::getRootTask() const
{
    return _abstr_tasks[_root_task_idx - _actions.size()];
}

const bool HtnInstance::isRootTask(const AbstractTask &task) const
{
    return task.getId() == _root_task_idx;
}

const std::vector<Predicate> &HtnInstance::getPredicates() const
{
    return _predicates;
}

// Action HtnInstance::getGoalAction() const
// {
//     return Action(/*id=*/_id_goal_action, /*preconditions=*/_goal_state, /*pos_effs=*/{}, /*neg_effs=*/{});
// }

const std::unordered_set<int> &HtnInstance::getInitState() const
{
    return _init_state;
}

const std::unordered_set<int> &HtnInstance::getGoalState() const
{
    return _goal_state;
}

const int HtnInstance::getNumPredicates() const
{
    return _predicates.size();
}

const std::vector<int> &HtnInstance::getFactVarsGoal() const
{
    return _all_fact_vars_goal;
}

const int HtnInstance::getNumMethods() const
{
    return _methods.size();
}

const int HtnInstance::getNumActions() const
{
    return _actions.size();
}

// Getters for method structure information
int HtnInstance::getMethodStructureId(int method_id) const
{
    auto it = _method_to_structure_id.find(method_id);
    if (it != _method_to_structure_id.end())
    {
        return it->second;
    }
    // Should not happen if all methods are processed correctly
    Log::e("Error: Structure ID not found for method_id %d\n", method_id);
    return -1; 
}

int HtnInstance::getNumSubtasksForStructure(int structure_id) const
{
    auto it = _structure_id_to_details.find(structure_id);
    if (it != _structure_id_to_details.end())
    {
        return it->second.first; // num_subtasks
    }
    Log::e("Error: Details not found for structure_id %d\n", structure_id);
    return -1;
}

const std::vector<std::pair<int, int>>& HtnInstance::getCanonicalOrderingConstraintsForStructure(int structure_id) const
{
    auto it = _structure_id_to_details.find(structure_id);
    if (it != _structure_id_to_details.end())
    {
        return it->second.second; // canonical_ordering_constraints
    }
    Log::e("Error: Details not found for structure_id %d when getting constraints\n", structure_id);
    // Return a static empty vector to avoid issues with dangling references
    static const std::vector<std::pair<int, int>> empty_constraints;
    return empty_constraints;
}

void HtnInstance::addInitAndGoalActionsToRootMethod()
{
    // Get the root method
    const AbstractTask &root_task = getRootTask();
    int root_method_id = root_task.getDecompositionMethodsIdx()[0];
    Method &root_method = _methods[root_method_id];

    int num_subtasks = root_method.getSubtasksIdx().size();

    // Add init subtask
    root_method.addSubtask(_id_init_action);
    int idx_init_action = num_subtasks;
    // Init is before all other subtasks
    for (int idx_subtask = 0; idx_subtask < num_subtasks; ++idx_subtask)
    {
        root_method.addOrderingConstraint(idx_init_action, idx_subtask);
    }

    // Add goal subtask
    root_method.addSubtask(_id_goal_action);
    int idx_goal_action = num_subtasks + 1;
    // Goal is after all other subtasks
    for (int idx_subtask = 0; idx_subtask < num_subtasks; ++idx_subtask)
    {
        root_method.addOrderingConstraint(idx_subtask, idx_goal_action);
    }
}
