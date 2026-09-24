#include "dependency_graph.hpp"

#include <algorithm>

namespace {
constexpr int kVisiting = 1;
constexpr int kDone = 2;
}

void DependencyGraph::addDependency(const std::string& service,
                                     const std::string& dependency) {
    auto& dependencies = graph[service];
    if (std::find(dependencies.begin(), dependencies.end(), dependency) ==
        dependencies.end()) {
        dependencies.push_back(dependency);
    }
}

void DependencyGraph::setDependencies(
    const std::string& service,
    const std::vector<std::string>& dependencies) {
    graph[service].clear();
    for (const auto& dependency : dependencies) {
        addDependency(service, dependency);
    }
}

bool DependencyGraph::hasDependency(const std::string& service,
                                     const std::string& dependency) const {
    auto it = graph.find(service);
    if (it == graph.end()) {
        return false;
    }

    return std::find(it->second.begin(), it->second.end(), dependency) !=
           it->second.end();
}

std::vector<std::string> DependencyGraph::getDependencies(
    const std::string& service) const {
    auto it = graph.find(service);
    if (it == graph.end()) {
        return {};
    }

    return it->second;
}

std::vector<std::string> DependencyGraph::getDependents(
    const std::string& service) const {
    std::vector<std::string> dependents;
    for (const auto& [name, dependencies] : graph) {
        if (std::find(dependencies.begin(), dependencies.end(), service) !=
            dependencies.end()) {
            dependents.push_back(name);
        }
    }
    return dependents;
}

bool DependencyGraph::wouldCreateCycle(
    const std::string& service,
    const std::vector<std::string>& dependencies) const {
    DependencyGraph candidate = *this;
    candidate.setDependencies(service, dependencies);

    std::vector<std::string> order;
    return !candidate.startOrder(service, order);
}

bool DependencyGraph::startOrder(const std::string& service,
                                 std::vector<std::string>& order) const {
    std::map<std::string, int> marks;
    order.clear();
    return visit(service, marks, order);
}

bool DependencyGraph::visit(const std::string& service,
                            std::map<std::string, int>& marks,
                            std::vector<std::string>& order) const {
    int& mark = marks[service];
    if (mark == kDone) {
        return true;
    }
    if (mark == kVisiting) {
        return false;
    }

    mark = kVisiting;
    for (const auto& dependency : getDependencies(service)) {
        if (!visit(dependency, marks, order)) {
            return false;
        }
    }
    marks[service] = kDone;
    order.push_back(service);
    return true;
}
