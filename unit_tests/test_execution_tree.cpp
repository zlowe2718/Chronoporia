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

    tree.RevertToState(1, 1, 1);
    tree.AddChild(/*T3=*/3, 1, 2, 2);

    REQUIRE(tree.GetState(1, 2) == 3);
}

TEST_CASE("RevertToState can branch off a node that is not the most recently added one", "[execution_tree]") {
    // T1
    // |- T2 -> T4 (run 1)
    // `- (after reverting to T1) T5 (run 2, branched from T1)
    ExecutionTree<int> tree(/*T1=*/1, 1, 0, 0);
    tree.AddChild(/*T2=*/2, 1, 1, 1);

    tree.RevertToState(1, 1, 1);
    tree.AddChild(/*T4=*/4, 1, 2, 2);

    tree.RevertToState(1, 0, 2);
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
    tree.RevertToState(1, 1, 1);

    tree.AddChild(/*T3=*/3, 1, 2, 2);

    tree.RevertToState(1, 1, 2);
    tree.AddChild(/*T4=*/4, 2, 0, 3);
    tree.RevertToState(2, 0, 2);
    tree.AddChild(/*T5=*/5, 2, 1, 4);

    tree.RevertToState(1, 0, 3);
    tree.AddChild(/*T6=*/6, 3, 0, 5);
    tree.RevertToState(3, 0, 3);
    tree.AddChild(/*T7=*/7, 3, 1, 6);
    tree.RevertToState(3, 1, 3);
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

TEST_CASE("GetState falls back to the closest earlier run_seq within the same run", "[execution_tree]") {
    // T1 -> T2 (run_seq 1) -> T3 (run_seq 5)
    ExecutionTree<int> tree(/*T1=*/1, 1, 0, 0);
    tree.AddChild(/*T2=*/2, 1, 1, 1);
    tree.RevertToState(1, 1, 1);
    tree.AddChild(/*T3=*/3, 1, 5, 2);

    // No node at run_seq 3, but T2 (run_seq 1) is the closest one back in time.
    REQUIRE(tree.GetState(1, 3) == 2);
    // Exact matches still work.
    REQUIRE(tree.GetState(1, 5) == 3);
}

TEST_CASE("GetState climbs to the branch point when the run hasn't started yet", "[execution_tree]") {
    // T1 (run 1, seq 0)
    // `- T2 -> T3 (run 2, seq 5 and 6, branched from T1)
    ExecutionTree<int> tree(/*T1=*/1, 1, 0, 0);
    tree.AddChild(/*T2=*/2, 2, 5, 1);
    tree.RevertToState(2, 5, 2);
    tree.AddChild(/*T3=*/3, 2, 6, 2);

    // run 2 doesn't have anything at or before seq 2, so fall back to the branch point T1.
    REQUIRE(tree.GetState(2, 2) == 1);
}

TEST_CASE("GetState climbs through multiple branch points when needed", "[execution_tree]") {
    // T1 (run 1, seq 0)
    // `- T2 (run 2, seq 0, branched from T1)
    //    `- T3 -> T4 (run 3, seq 10 and 11, branched from T2)
    ExecutionTree<int> tree(/*T1=*/1, 1, 0, 0);
    tree.AddChild(/*T2=*/2, 2, 0, 1);
    tree.RevertToState(2, 0, 3);
    tree.AddChild(/*T3=*/3, 3, 10, 2);
    tree.RevertToState(3, 10, 3);
    tree.AddChild(/*T4=*/4, 3, 11, 3);

    // run 3 hasn't started at seq 4, climb past the whole run to its branch point T2.
    REQUIRE(tree.GetState(3, 4) == 2);
}

TEST_CASE("GetState returns nullopt when the run is unknown and there is nothing to fall back to", "[execution_tree]") {
    ExecutionTree<int> tree(/*T1=*/1, 1, 0, 0);

    REQUIRE(tree.GetState(99, 0) == std::nullopt);
}

TEST_CASE("GetState returns nullopt when the target predates the root", "[execution_tree]") {
    ExecutionTree<int> tree(/*T1=*/1, 1, 5, 0);

    REQUIRE(tree.GetState(1, 2) == std::nullopt);
}

TEST_CASE("works with non-trivial Data types via both copy and move construction", "[execution_tree]") {
    std::string initial = "root";
    ExecutionTree<std::string> tree(initial, 1, 0, 0);

    tree.AddChild(std::string("child"), 1, 1, 1);

    REQUIRE(tree.GetState(1, 0) == "root");
    REQUIRE(tree.GetState(1, 1) == "child");
}

namespace {

// Builds the tree documented at the top of execution_tree.h:
//
// T1 (1,1) -- T2 (1,2) -- T3 (1,3)
//                |          `-- (run 4, empty, branched from T3)
//                `-- T4 (2,4) -> T5 (2,5)       (run 2, branched from T2)
// T1 -- T6 (3,10) -> T7 (3,11) -> T8 (3,12)     (run 3, branched from T1)
ExecutionTree<int> BuildDocumentedExampleTree() {
    ExecutionTree<int> tree(/*T1=*/1, /*run_id=*/1, /*run_seq=*/1, /*global_seq=*/0);

    tree.AddChild(/*T2=*/2, 1, 2, 1);
    tree.RevertToState(1, 2, 1);
    tree.AddChild(/*T3=*/3, 1, 3, 2);

    // Branch run 4 off T3, but never add anything to it -- it stays empty.
    tree.RevertToState(1, 3, 4);

    // Branch run 2 off T2: T4 -> T5.
    tree.RevertToState(1, 2, 2);
    tree.AddChild(/*T4=*/4, 2, 4, 3);
    tree.RevertToState(2, 4, 2);
    tree.AddChild(/*T5=*/5, 2, 5, 4);

    // Branch run 3 off T1: T6 -> T7 -> T8.
    tree.RevertToState(1, 1, 3);
    tree.AddChild(/*T6=*/6, 3, 10, 5);
    tree.RevertToState(3, 10, 3);
    tree.AddChild(/*T7=*/7, 3, 11, 6);
    tree.RevertToState(3, 11, 3);
    tree.AddChild(/*T8=*/8, 3, 12, 7);

    return tree;
}

}  // namespace

TEST_CASE("FindNode returns nullopt for targets before the tree starts", "[execution_tree][find_node]") {
    ExecutionTree<int> tree = BuildDocumentedExampleTree();

    // run_id before the root's run_id entirely.
    REQUIRE(tree.FindNode(0, 0) == std::nullopt);
    // Same run as root, but before the root's own run_seq.
    REQUIRE(tree.FindNode(1, 0) == std::nullopt);
}

TEST_CASE("FindNode returns the exact node on a direct match", "[execution_tree][find_node]") {
    ExecutionTree<int> tree = BuildDocumentedExampleTree();

    REQUIRE((*tree.FindNode(1, 1))->state == 1);  // T1
    REQUIRE((*tree.FindNode(1, 2))->state == 2);  // T2
    REQUIRE((*tree.FindNode(1, 3))->state == 3);  // T3
    REQUIRE((*tree.FindNode(2, 4))->state == 4);  // T4
    REQUIRE((*tree.FindNode(2, 5))->state == 5);  // T5
    REQUIRE((*tree.FindNode(3, 10))->state == 6); // T6
    REQUIRE((*tree.FindNode(3, 11))->state == 7); // T7
    REQUIRE((*tree.FindNode(3, 12))->state == 8); // T8
}

TEST_CASE("FindNode falls back to the empty branch's origin when the run has no nodes", "[execution_tree][find_node]") {
    ExecutionTree<int> tree = BuildDocumentedExampleTree();

    // Run 4 was branched off T3 but never got any nodes -- any seq in run 4
    // should resolve back to T3, regardless of how large the target seq is.
    REQUIRE((*tree.FindNode(4, 7))->state == 3);
    REQUIRE((*tree.FindNode(4, 0))->state == 3);
    REQUIRE((*tree.FindNode(4, 1000))->state == 3);
}

TEST_CASE("FindNode returns the latest node in a run when target_run_seq exceeds it", "[execution_tree][find_node]") {
    ExecutionTree<int> tree = BuildDocumentedExampleTree();

    // run 2's latest node is T5 (seq 5); anything beyond that should still resolve to T5.
    REQUIRE((*tree.FindNode(2, 6))->state == 5);
    REQUIRE((*tree.FindNode(2, 1000))->state == 5);

    // run 3's latest node is T8 (seq 12).
    REQUIRE((*tree.FindNode(3, 100))->state == 8);
}

TEST_CASE("FindNode falls back to the branch point when the run hasn't started yet", "[execution_tree][find_node]") {
    ExecutionTree<int> tree = BuildDocumentedExampleTree();

    // run 2's earliest node is T4 (seq 4); seq 3 is before run 2 started, so
    // fall back to T2, the node run 2 branched from.
    REQUIRE((*tree.FindNode(2, 3))->state == 2);

    // run 3's earliest node is T6 (seq 10); seq 9 is before run 3 started, so
    // fall back to T1, the node run 3 branched from.
    REQUIRE((*tree.FindNode(3, 9))->state == 1);
}

TEST_CASE("FindNode returns nullopt for an unknown run with nothing to fall back to", "[execution_tree][find_node]") {
    ExecutionTree<int> tree = BuildDocumentedExampleTree();

    // Run 99 was never branched anywhere in the tree.
    REQUIRE(tree.FindNode(99, 0) == std::nullopt);
}

TEST_CASE("FindNode does not cross into sibling branches that postdate the target run", "[execution_tree][find_node]") {
    ExecutionTree<int> tree = BuildDocumentedExampleTree();

    // Querying run 2 should never resolve to anything from the unrelated run 3 branch.
    auto result = tree.FindNode(2, 1000);
    REQUIRE(result.has_value());
    REQUIRE((*result)->run_id == 2);
    REQUIRE((*result)->state == 5);
}

TEST_CASE("RevertToState starts a fresh branch off the root when the target isn't found", "[execution_tree]") {
    ExecutionTree<int> tree(/*T1=*/1, 1, 1, 0);

    // (99, 5) doesn't exist anywhere in the tree, so this should branch off
    // the tree's hidden root rather than asserting/crashing.
    tree.RevertToState(99, 5, 42);
    tree.AddChild(/*orphan=*/100, 42, 0, 1);

    REQUIRE(tree.GetState(42, 0) == 100);
    // The original branch is untouched by the orphan branch.
    REQUIRE(tree.GetState(1, 1) == 1);
}

TEST_CASE("RevertToState's fallback branch off the root is queryable like any other branch", "[execution_tree]") {
    ExecutionTree<int> tree(/*T1=*/1, 1, 1, 0);

    tree.RevertToState(99, 5, 42);
    tree.AddChild(/*Orphan1=*/100, 42, 1, 1);
    tree.RevertToState(42, 1, 42);
    tree.AddChild(/*Orphan2=*/101, 42, 2, 2);

    // Exact match.
    REQUIRE(tree.GetState(42, 1) == 100);
    // Past the end of the orphan run resolves to its latest node.
    REQUIRE(tree.GetState(42, 100) == 101);
    // Querying the never-found target itself still resolves to nothing,
    // since the fallback branch lives off the root, not off (99, 5).
    REQUIRE(tree.GetState(99, 5) == std::nullopt);
}
