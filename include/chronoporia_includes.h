#pragma once

#ifndef CHRONOPORIA_TRACING
    #if defined(_MSC_VER)
        #include <intrin.h>
        // This is a macro that injects 5 no-op statements so Chronoporia can inject branch counting safely for tight loops
        // This would be used for something like the following where we wouldn't have enough instructions in the branch without
        // having to push code around (which is not desirable)
        //
        // while (true) {
        //     i++;
        // }
        // 
        // then becomes
        //
        // while (true) {
        //     CHRONOPORIA_INCREMENT_BRANCH_COUNT():
        //     i++;
        // }
        //         
        #define CHRONOPORIA_INCREMENT_BRANCH_COUNT() \
            do { \
                __nop(); __nop(); __nop(); __nop(); __nop(); \
            } while (0)
    #endif
#else
    #define CHRONOPORIA_INCREMENT_BRANCH_COUNT() do {} while (0)
#endif