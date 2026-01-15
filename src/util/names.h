// htn_printer.h
#ifndef HTN_PRINTER_H
#define HTN_PRINTER_H

#include <string>
#include "data/action.h"
#include "data/method.h"
#include "data/predicate.h"
#include "data/abstract_task.h"
#include "data/htn_instance.h"
#include "data/pdt_node.h"

#define TOSTR(x) Names::to_string(x).c_str()

namespace Names {
    void init(std::vector<Predicate>& predicates, std::vector<Action>& actions, std::vector<AbstractTask>& abstr_tasks, std::vector<Method>& methods, const Action* blankAction, const Action* init_action, const Action* goal_action);
    std::string to_string(const Action& a);
    std::string to_string(const Predicate& p);
    std::string to_string(const AbstractTask& at);
    std::string to_string(const Method& m);
    std::string to_string(const PdtNode& node);
    std::string to_string(const OrderingConstrains& oc);
}

#endif // HTN_PRINTER_H