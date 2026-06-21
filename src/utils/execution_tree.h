#include <cassert>
#include <cstdint>
#include <memory>
#include <vector>
#include <map>

namespace chronoporia {

// T1 
// ├── T2
// │   ├── T3          (run 1)
// │   └── T4 → T5    (run 2, branched from T2)
// └── T6 → T7 → T8   (run 3, branched from T1)

    // TODO: add optimizations later?  Like max run_id and global_seq of children nodes for faster traversal?
    template <typename Data>
    class ExecutionTree {
    public:
        struct ExecutionNode {
            Data state;
            uint32_t run_id;
            uint32_t run_seq;
            uint64_t global_seq;
            std::map<uint32_t, std::vector<std::unique_ptr<ExecutionNode>>> branch_children;

            ExecutionNode(Data&& state, uint32_t run_id, uint32_t run_seq, uint64_t global_seq) 
                : state {state}
                , run_id {run_id}
                , run_seq {run_seq}
                , global_seq {global_seq}
                , branch_children {}
            {};

            ExecutionNode(const Data& state, uint32_t run_id, uint32_t run_seq, uint64_t global_seq) 
                : state {state}
                , run_id {run_id}
                , run_seq {run_seq}
                , global_seq {global_seq}
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
            primary_branch_.push_back(std::make_unique<ExecutionNode>(state, run_id, run_seq, global_seq));
            cursor_ = primary_branch_[0].get();
        }

        ExecutionTree(const Data& state, uint32_t run_id, uint32_t run_seq, uint64_t global_seq)
        {
            primary_branch_.push_back(std::make_unique<ExecutionNode>(state, run_id, run_seq, global_seq));
            cursor_ = primary_branch_[0].get();
        }

        void AddChild(Data&& state, uint32_t run_id, uint32_t run_seq, uint64_t global_seq) {
            auto node = std::make_unique<ExecutionNode>(state, run_id, run_seq, global_seq);
            
            // If run branch doesn't exist this will automatically create a new branch
            cursor_->branch_children[run_id].push_back(std::move(node));
        }

        Data GetState(uint32_t target_run_id, uint32_t target_run_seq) const {
            std::vector<ExecutionNode*> stack {};

            for (const auto& primary_node : primary_branch_) {
                stack.push_back(primary_node.get());
            }

            ExecutionNode *node = nullptr;
            bool found = false;

            while (!stack.empty()) {
                node = stack.back();
                stack.pop_back();

                if (node->run_id == target_run_id && node->run_seq == target_run_seq) {
                    found = true;
                    break;
                }

                for (const auto& [_, children] : node->branch_children) {
                    for (const auto& child: children) {
                        stack.push_back(child.get());
                    }
                }
            }

            // Ensure we found the node
            assert(found);
            return node->state;
        }

        void RevertToState(uint32_t target_run_id, uint32_t target_run_seq) {
            std::vector<ExecutionNode*> stack {};

            for (const auto& primary_node : primary_branch_) {
                stack.push_back(primary_node.get());
            }

            while (!stack.empty()) {
                ExecutionNode* node = stack.back();
                stack.pop_back();

                if (node->run_id == target_run_id && node->run_seq == target_run_seq) {
                    cursor_ = node;
                    return;
                }

                for (const auto& [_, children] : node->branch_children) {
                    for (const auto& child : children) {
                        stack.push_back(child.get());
                    }
                }
            }

            // Target node not found in tree
            assert(false);
        }

    private:
        std::vector<std::unique_ptr<ExecutionNode>> primary_branch_;
        // For simplicity this cursor is the current parent
        ExecutionNode* cursor_;
    };

}