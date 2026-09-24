#ifndef DEPENDENCY_GRAPH_HPP
#define DEPENDENCY_GRAPH_HPP

#include <string>
#include <map>
#include <vector>

class DependencyGraph {
public:
    void addDependency(const std::string& service,
                       const std::string& dependency);

    // Replaces all dependencies of `service`.
    void setDependencies(const std::string& service,
                         const std::vector<std::string>& dependencies);

    bool hasDependency(const std::string& service,
                       const std::string& dependency) const;

    std::vector<std::string> getDependencies(
        const std::string& service) const;

    // Services that directly depend on `service`.
    std::vector<std::string> getDependents(
        const std::string& service) const;

    // True if giving `service` these dependencies would create a cycle.
    bool wouldCreateCycle(const std::string& service,
                          const std::vector<std::string>& dependencies) const;

    // `service` and everything it transitively depends on, dependencies
    // first. Returns false if a cycle is found.
    bool startOrder(const std::string& service,
                    std::vector<std::string>& order) const;

private:
    bool visit(const std::string& service,
               std::map<std::string, int>& marks,
               std::vector<std::string>& order) const;

    std::map<std::string, std::vector<std::string>> graph;
};

#endif
