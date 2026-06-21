#include <catch2/catch_test_macros.hpp>

#include "execution_tree.h"

#include <string>

using chronoporia::ExecutionTree;

TEST_CASE("root node is queryable immediately after construction", "[execution_tree]") {
    ExecutionTree<int> tree(42, /*run_id=*/1, /*run_seq=*/0, /*global_seq=*/0);

    REQUIRE(tree.GetState(1, 0) == 42);
}

TEST_CASE("AddChild attaches a node under the current cursor", "[execution_tree]") {
    ExecutionTree<int> tree(1, 1, 0, 0);

    tree.AddChild(2, 1, 1, 1);

    REQUIRE(tree.GetState(1, 0) == 1);
    REQUIRE(tree.GetState(1, 1) == 2);
}

TEST_CASE("AddChild under the same run_id appends siblings to one branch", "[execution_tree]") {
    ExecutionTree<int> tree(1, 1, 0, 0);

    tree.AddChild(2, 1, 1, 1);
    tree.AddChild(3, 1, 2, 2);

    REQUIRE(tree.GetState(1, 1) == 2);
    REQUIRE(tree.GetState(1, 2) == 3);
}

TEST_CASE("AddChild under a different run_id creates a separate branch from the cursor", "[execution_tree]") {
    // T1
    // |- T2 (run 1)
    // `- T6 (run 3, branched from T1)
    ExecutionTree<int> tree(/*T1=*/1, 1, 0, 0);

    tree.AddChild(/*T2=*/2, 1, 1, 1);
    tree.AddChild(/*T6=*/6, 3, 0, 2);

    REQUIRE(tree.GetState(1, 1) == 2);
    REQUIRE(tree.GetState(3, 0) == 6);
}

TEST_CASE("RevertToState moves the cursor so subsequent children attach to the new parent", "[execution_tree]") {
    // T1 -> T2 -> T3
    ExecutionTree<int> tree(/*T1=*/1, 1, 0, 0);
    tree.AddChild(/*T2=*/2, 1, 1, 1);

    tree.RevertToState(1, 1);
    tree.AddChild(/*T3=*/3, 1, 2, 2);

    REQUIRE(tree.GetState(1, 2) == 3);
}

TEST_CASE("RevertToState can branch off a node that is not the most recently added one", "[execution_tree]") {
    // T1
    // |- T2 -> T4 (run 1)
    // `- (after reverting to T1) T5 (run 2, branched from T1)
    ExecutionTree<int> tree(/*T1=*/1, 1, 0, 0);
    tree.AddChild(/*T2=*/2, 1, 1, 1);

    tree.RevertToState(1, 1);
    tree.AddChild(/*T4=*/4, 1, 2, 2);

    tree.RevertToState(1, 0);
    tree.AddChild(/*T5=*/5, 2, 0, 3);

    REQUIRE(tree.GetState(1, 2) == 4);
    REQUIRE(tree.GetState(2, 0) == 5);
}

TEST_CASE("matches the documented branching example", "[execution_tree]") {
    // T1
    // |-- T2
    // |   |-- T3          (run 1)
    // |   `-- T4 -> T5    (run 2, branched from T2)
    // `-- T6 -> T7 -> T8   (run 3, branched from T1)
    ExecutionTree<int> tree(/*T1=*/1, 1, 0, 0);

    tree.AddChild(/*T2=*/2, 1, 1, 1);
    tree.RevertToState(1, 1);

    tree.AddChild(/*T3=*/3, 1, 2, 2);

    tree.RevertToState(1, 1);
    tree.AddChild(/*T4=*/4, 2, 0, 3);
    tree.RevertToState(2, 0);
    tree.AddChild(/*T5=*/5, 2, 1, 4);

    tree.RevertToState(1, 0);
    tree.AddChild(/*T6=*/6, 3, 0, 5);
    tree.RevertToState(3, 0);
    tree.AddChild(/*T7=*/7, 3, 1, 6);
    tree.RevertToState(3, 1);
    tree.AddChild(/*T8=*/8, 3, 2, 7);

    REQUIRE(tree.GetState(1, 0) == 1);
    REQUIRE(tree.GetState(1, 1) == 2);
    REQUIRE(tree.GetState(1, 2) == 3);
    REQUIRE(tree.GetState(2, 0) == 4);
    REQUIRE(tree.GetState(2, 1) == 5);
    REQUIRE(tree.GetState(3, 0) == 6);
    REQUIRE(tree.GetState(3, 1) == 7);
    REQUIRE(tree.GetState(3, 2) == 8);
}

TEST_CASE("works with non-trivial Data types via both copy and move construction", "[execution_tree]") {
    std::string initial = "root";
    ExecutionTree<std::string> tree(initial, 1, 0, 0);

    tree.AddChild(std::string("child"), 1, 1, 1);

    REQUIRE(tree.GetState(1, 0) == "root");
    REQUIRE(tree.GetState(1, 1) == "child");
}
