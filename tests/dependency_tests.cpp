#include "../src/dependency/dependency_graph.hpp"
#include "test_util.hpp"

#include <algorithm>

namespace {

size_t indexOf(const std::vector<std::string>& items, const std::string& item) {
    return std::find(items.begin(), items.end(), item) - items.begin();
}

void testBasics() {
    DependencyGraph graph;
    graph.addDependency("web", "database");
    graph.addDependency("web", "database");  // no duplicates

    CHECK(graph.hasDependency("web", "database"));
    CHECK(!graph.hasDependency("web", "cache"));
    CHECK(graph.getDependencies("web").size() == 1);
    CHECK(graph.getDependencies("unknown").empty());

    graph.setDependencies("web", {"cache"});
    CHECK(!graph.hasDependency("web", "database"));
    CHECK(graph.hasDependency("web", "cache"));
}

void testDependents() {
    DependencyGraph graph;
    graph.setDependencies("web", {"db", "cache"});
    graph.setDependencies("worker", {"db"});

    auto dependents = graph.getDependents("db");
    CHECK(dependents.size() == 2);
    CHECK(graph.getDependents("web").empty());
}

void testStartOrder() {
    DependencyGraph graph;
    graph.setDependencies("web", {"api", "cache"});
    graph.setDependencies("api", {"db"});
    graph.setDependencies("cache", {"db"});

    std::vector<std::string> order;
    CHECK(graph.startOrder("web", order));
    CHECK(order.size() == 4);  // db appears once
    CHECK(order.back() == "web");
    CHECK(indexOf(order, "db") < indexOf(order, "api"));
    CHECK(indexOf(order, "db") < indexOf(order, "cache"));

    CHECK(graph.startOrder("standalone", order));
    CHECK(order.size() == 1);
}

void testCycles() {
    DependencyGraph graph;
    graph.setDependencies("a", {"b"});
    graph.setDependencies("b", {"c"});

    CHECK(graph.wouldCreateCycle("c", {"a"}));
    CHECK(!graph.wouldCreateCycle("c", {"d"}));
    CHECK(!graph.hasDependency("c", "a"));  // check doesn't modify

    graph.setDependencies("c", {"a"});
    std::vector<std::string> order;
    CHECK(!graph.startOrder("a", order));
}

}  // namespace

int main() {
    testBasics();
    testDependents();
    testStartOrder();
    testCycles();
    return finishTests("dependency_tests");
}
