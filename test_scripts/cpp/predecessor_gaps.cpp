#include <cstdio>

// Cases where get_predecessors_of_address() cannot name a predecessor.
//
// Two things limit it: branch_sources_targeting() only matches a BranchSite whose
// target was resolved at analysis time (indirect branches and `ret` record 0), and
// fallthrough_predecessor() deliberately returns 0 when the preceding instruction is
// a call / jmp / ret.  Every case below arrives at its block leader through exactly
// those blind spots, so the candidate list comes back empty.

__declspec(noinline) int direct_callee(int x) {
    return x + 1;
}

// Only ever reached through the function pointer below -- never with a direct call,
// so no BranchSite in the module resolves to its entry.
__declspec(noinline) int pointer_callee(int x) {
    return x + 2;
}

struct Shape {
    virtual ~Shape() = default;
    virtual int sides() { return 0; }
};

struct Square : Shape {
    // Reached only through the vtable, same gap as pointer_callee.
    int sides() override { return 4; }
};

using callee_fn = int (*)(int);

// volatile so even a Debug build cannot fold the load back into a direct call.
static volatile callee_fn indirect = pointer_callee;

__declspec(noinline) int dispatch(int selector, int value) {
    int result = 0;
    // Dense enough that MSVC emits `jmp qword ptr [table + rax*8]`.  Each case body
    // is a block leader whose only in-function branch source is that indirect jmp.
    switch (selector) {
        case 0:  result = value + 10; break;
        case 1:  result = value + 20; break;
        case 2:  result = value + 30; break;
        case 3:  result = value + 40; break;
        default: result = -1;         break;
    }
    return result;
}

int main() {
    const int seed = 1;

    // CASE 1 -- the return site of a DIRECT call.
    // The instruction after `call direct_callee` (the store of eax into `direct`) is
    // not a block leader, because discover_blocks() does not split on calls.  So
    // branch_sources_targeting() is never consulted, and fallthrough_predecessor()
    // rejects the call that precedes it.  The real predecessor is the callee's `ret`,
    // which is invisible either way.
    const int direct = direct_callee(seed);
    printf("direct=%d\n", direct);

    // CASE 2 -- the entry of an INDIRECTLY called function.
    // pointer_callee's entry IS a leader, but the only branch reaching it is
    // `call qword ptr [indirect]`, whose recorded target is 0.  The entry has no
    // fallthrough predecessor either (fallthrough_predecessor bails on fn.info.start).
    const int through_pointer = indirect(seed);
    printf("through_pointer=%d\n", through_pointer);

    // CASE 3 -- virtual dispatch.  Same gap as case 2, reached via the vtable.
    // Step into the call, then reverse step from the first line of Square::sides.
    Shape* shape = new Square();
    const int sides = shape->sides();
    printf("sides=%d\n", sides);
    delete shape;

    // CASE 4 -- switch jump table.
    // Reverse step from the first instruction of any `case` body: its predecessor is
    // the indirect jmp through the table, so no candidate is produced.
    for (int selector = 0; selector < 4; ++selector) {
        printf("dispatch(%d)=%d\n", selector, dispatch(selector, seed));
    }

    return 0;
}
