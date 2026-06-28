#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <map>

namespace chronoporia {

// T1 ── T2 ── T3         (run 1)
// │      │    └─         (run 4, empty branched from T3) 
// │      └── T4 → T5     (run 2, branched from T2)
// └── T6 → T7 → T8       (run 3, branched from T1)

    // TODO: add optimizations later?  Like max run_id and global_seq of children nodes for faster traversal?
    template <typename Data>
    class ExecutionTree {
    public:
        struct ExecutionNode {
            Data state;
            uint32_t run_id;
            uint32_t run_seq;
            uint64_t global_seq;
            ExecutionNode* parent;
            std::map<uint32_t, std::vector<std::unique_ptr<ExecutionNode>>> branch_children;

            ExecutionNode(Data&& state, uint32_t run_id, uint32_t run_seq, uint64_t global_seq)
                : state {state}
                , run_id {run_id}
                , run_seq {run_seq}
                , global_seq {global_seq}
                , parent {nullptr}
                , branch_children {}
            {};

            ExecutionNode(const Data& state, uint32_t run_id, uint32_t run_seq, uint64_t global_seq)
                : state {state}
                , run_id {run_id}
                , run_seq {run_seq}
                , global_seq {global_seq}
                , parent {nullptr}
                , branch_children {}
            {};
        };

        ExecutionTree() = delete;
        ~ExecutionTree() = default;

        ExecutionTree(const ExecutionTree&) = delete;
        ExecutionTree& operator=(const ExecutionTree&) = delete;

        ExecutionTree(ExecutionTree&&) = default;
        ExecutionTree& operator=(ExecutionTree&&) = default;

        ExecutionTree(Data&& state, uint32_t run_id, uint32_t run_seq, uint64_t global_seq)
        {
            tree_node_ = std::make_unique<ExecutionNode>(Data {}, 0, 0, 0);

            auto node = std::make_unique<ExecutionNode>(std::move(state), run_id, run_seq, global_seq);
            node->parent = tree_node_.get();
            cursor_ = node.get();
            tree_node_->branch_children[run_id].push_back(std::move(node));
        }

        ExecutionTree(const Data& state, uint32_t run_id, uint32_t run_seq, uint64_t global_seq)
        {
            tree_node_ = std::make_unique<ExecutionNode>(Data {}, 0, 0, 0);

            auto node = std::make_unique<ExecutionNode>(state, run_id, run_seq, global_seq);
            node->parent = tree_node_.get();
            cursor_ = node.get();
            tree_node_->branch_children[run_id].push_back(std::move(node));
        }

        void AddChild(Data&& state, uint32_t run_id, uint32_t run_seq, uint64_t global_seq) {
            auto node = std::make_unique<ExecutionNode>(state, run_id, run_seq, global_seq);
            node->parent = cursor_;

            // If run branch doesn't exist this will automatically create a new branch
            cursor_->branch_children[run_id].push_back(std::move(node));
        }

        // If a direct match is not found then look for the closest back in time (going up and backwards):
        // the node in the target run with the highest run_seq <= target_run_seq, or if the run hasn't
        // reached target_run_seq yet, the node the run branched from (and so on up the tree).
        // Returns nullopt if none found
        std::optional<Data> GetState(uint32_t target_run_id, uint32_t target_run_seq) const {
            const auto& node = FindNode(target_run_id, target_run_seq);

            if (node == std::nullopt) {
                return std::nullopt;
            }

            return (*node)->state;
        }

        // Makes a new empty branch on the reverted node so we always have all branch knowledge.
        // If the target isn't found anywhere in the tree, the new branch is made off of
        // tree_node_ instead, starting a fresh, disconnected run.
        void RevertToState(uint32_t target_run_id, uint32_t target_run_seq, uint64_t new_run_id) {
            const auto& node = FindNode(target_run_id, target_run_seq);
            cursor_ = node.has_value() ? *node : tree_node_.get();

            cursor_->branch_children[new_run_id];
        }

        // Returns the exact node if there is a match for run_id and run_seq.  Otherwise returns the closest node
        // that was created before run_id and run_seq.
        std::optional<ExecutionNode *> FindNode(uint32_t target_run_id, uint32_t target_run_seq) const {
            std::vector<ExecutionNode*> stack {};

            for (const auto& [_, children] : tree_node_->branch_children) {
                for (const auto& child : children) {
                    stack.push_back(child.get());
                }
            }

            // Highest run_seq <= target_run_seq seen so far within the target run.
            ExecutionNode* best_in_run = nullptr;
            // The node the target run branched from, in case the target run has no
            // node at or before target_run_seq (or has no nodes at all, e.g. an empty branch).
            ExecutionNode* origin = nullptr;

            while (!stack.empty()) {
                ExecutionNode* node = stack.back();
                stack.pop_back();

                if (target_run_id == node->run_id && target_run_seq == node->run_seq) {
                    return node;
                }

                if (node->run_id == target_run_id && node->run_seq <= target_run_seq &&
                    (best_in_run == nullptr || node->run_seq > best_in_run->run_seq)) {
                    best_in_run = node;
                }

                // look through all branches and only push branch children with a run_id <= target_run_id
                // For example if we have branches 2, 3, 5 and we're looking for 4 then look at 2, and 3's children because
                // 4 cannot be a branch of 5 which is later in time
                for (const auto& [run_id, children] : node->branch_children) {
                    if (run_id == target_run_id && node->run_id != target_run_id) {
                        origin = node;
                    }

                    if (run_id <= target_run_id) {
                        for (const auto& child : children) {
                            stack.push_back(child.get());
                        }
                    }
                }
            }

            if (best_in_run != nullptr) {
                return best_in_run;
            }

            if (origin != nullptr) {
                return origin;
            }

            return std::nullopt;
        }

        bool ExactNodeExists(uint32_t target_run_id, uint32_t target_run_seq) {
            std::vector<ExecutionNode*> stack {};

            for (const auto& [_, children] : tree_node_->branch_children) {
                for (const auto& child : children) {
                    stack.push_back(child.get());
                }
            }

            while (!stack.empty()) {
                ExecutionNode* node = stack.back();
                stack.pop_back();

                if (target_run_id == node->run_id && target_run_seq == node->run_seq) {
                    return true;
                }

                // look through all branches and only push branch children with a run_id <= target_run_id
                // For example if we have branches 2, 3, 5 and we're looking for 4 then look at 2, and 3's children because
                // 4 cannot be a branch of 5 which is later in time
                for (const auto& [run_id, children] : node->branch_children) {
                    if (run_id <= target_run_id) {
                        for (const auto& child : children) {
                            stack.push_back(child.get());
                        }
                    }
                }
            }
            return false;           
        }

        std::optional<ExecutionNode *> GetExactNode(uint32_t target_run_id, uint32_t target_run_seq) {
            std::vector<ExecutionNode*> stack {};

            for (const auto& [_, children] : tree_node_->branch_children) {
                for (const auto& child : children) {
                    stack.push_back(child.get());
                }
            }

            while (!stack.empty()) {
                ExecutionNode* node = stack.back();
                stack.pop_back();

                if (target_run_id == node->run_id && target_run_seq == node->run_seq) {
                    return node;
                }

                // look through all branches and only push branch children with a run_id <= target_run_id
                // For example if we have branches 2, 3, 5 and we're looking for 4 then look at 2, and 3's children because
                // 4 cannot be a branch of 5 which is later in time
                for (const auto& [run_id, children] : node->branch_children) {
                    if (run_id <= target_run_id) {
                        for (const auto& child : children) {
                            stack.push_back(child.get());
                        }
                    }
                }
            }
            return std::nullopt;
        }


        // Walks every branch of the tree depth-first, calling formatter(state, run_id, run_seq, global_seq)
        // for each node to get its display line. Child branches are printed indented under their parent.
        template <typename Formatter>
        void Print(Formatter&& formatter) const {
            for (const auto& [run_id, children] : tree_node_->branch_children) {
                for (const auto& child : children) {
                    PrintNode(child.get(), formatter, 0);
                }
            }
        }

    private:
        template <typename Formatter>
        void PrintNode(ExecutionNode* node, Formatter& formatter, int depth) const {
            std::string indent(static_cast<size_t>(depth) * 2, ' ');
            printf("%s%s\n", indent.c_str(), formatter(node->state, node->run_id, node->run_seq, node->global_seq).c_str());

            for (const auto& [run_id, children] : node->branch_children) {
                for (const auto& child : children) {
                    PrintNode(child.get(), formatter, depth + 1);
                }
            }
        }

        std::unique_ptr<ExecutionNode> tree_node_;
        // For simplicity this cursor is the current parent
        ExecutionNode* cursor_;
    };

}