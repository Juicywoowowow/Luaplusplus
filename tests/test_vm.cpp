/*
 * test_vm.cpp - Tests for Lua++ virtual machine
 * 
 * Tests interpretation of Lua++ code including expressions,
 * control flow, functions, closures, and error handling.
 */

#include <gtest/gtest.h>
#include <sstream>
#include <cstdio>

extern "C" {
#include "vm.h"
#include "compiler.h"
}

// Helper to capture stdout during interpretation
class OutputCapture {
    std::streambuf* oldCout;
    std::ostringstream capture;
public:
    OutputCapture() {
        oldCout = std::cout.rdbuf(capture.rdbuf());
    }
    ~OutputCapture() {
        std::cout.rdbuf(oldCout);
    }
    std::string get() { return capture.str(); }
};

// ============== Basic Interpretation Tests ==============

class VMBasicTest : public ::testing::Test {
protected:
    void SetUp() override {
        initVM();
    }
    void TearDown() override {
        freeVM();
    }
};

TEST_F(VMBasicTest, EmptyProgram) {
    InterpretResult result = interpret("");
    EXPECT_EQ(result, INTERPRET_OK);
}

TEST_F(VMBasicTest, SimpleExpression) {
    InterpretResult result = interpret("local x = 1 + 2");
    EXPECT_EQ(result, INTERPRET_OK);
}

TEST_F(VMBasicTest, ArithmeticOperations) {
    EXPECT_EQ(interpret("local x = 1 + 2"), INTERPRET_OK);
    EXPECT_EQ(interpret("local x = 10 - 3"), INTERPRET_OK);
    EXPECT_EQ(interpret("local x = 4 * 5"), INTERPRET_OK);
    EXPECT_EQ(interpret("local x = 20 / 4"), INTERPRET_OK);
    EXPECT_EQ(interpret("local x = 17 % 5"), INTERPRET_OK);
}

TEST_F(VMBasicTest, UnaryOperations) {
    EXPECT_EQ(interpret("local x = -5"), INTERPRET_OK);
    EXPECT_EQ(interpret("local x = not true"), INTERPRET_OK);
    EXPECT_EQ(interpret("local x = not false"), INTERPRET_OK);
}

TEST_F(VMBasicTest, ComparisonOperations) {
    EXPECT_EQ(interpret("local x = 1 < 2"), INTERPRET_OK);
    EXPECT_EQ(interpret("local x = 2 > 1"), INTERPRET_OK);
    EXPECT_EQ(interpret("local x = 1 <= 1"), INTERPRET_OK);
    EXPECT_EQ(interpret("local x = 2 >= 2"), INTERPRET_OK);
    EXPECT_EQ(interpret("local x = 1 == 1"), INTERPRET_OK);
    EXPECT_EQ(interpret("local x = 1 ~= 2"), INTERPRET_OK);
}

TEST_F(VMBasicTest, LogicalOperations) {
    EXPECT_EQ(interpret("local x = true and true"), INTERPRET_OK);
    EXPECT_EQ(interpret("local x = true or false"), INTERPRET_OK);
    EXPECT_EQ(interpret("local x = false and true"), INTERPRET_OK);
    EXPECT_EQ(interpret("local x = false or true"), INTERPRET_OK);
}

TEST_F(VMBasicTest, StringConcatenation) {
    EXPECT_EQ(interpret("local x = \"hello\" .. \" \" .. \"world\""), INTERPRET_OK);
}

TEST_F(VMBasicTest, Literals) {
    EXPECT_EQ(interpret("local x = nil"), INTERPRET_OK);
    EXPECT_EQ(interpret("local x = true"), INTERPRET_OK);
    EXPECT_EQ(interpret("local x = false"), INTERPRET_OK);
    EXPECT_EQ(interpret("local x = 42"), INTERPRET_OK);
    EXPECT_EQ(interpret("local x = 3.14"), INTERPRET_OK);
    EXPECT_EQ(interpret("local x = \"string\""), INTERPRET_OK);
}

// ============== Variable Tests ==============

class VMVariableTest : public ::testing::Test {
protected:
    void SetUp() override { initVM(); }
    void TearDown() override { freeVM(); }
};

TEST_F(VMVariableTest, LocalVariable) {
    EXPECT_EQ(interpret("local x = 10"), INTERPRET_OK);
}

TEST_F(VMVariableTest, LocalVariableNoInit) {
    EXPECT_EQ(interpret("local x"), INTERPRET_OK);
}

TEST_F(VMVariableTest, MultipleLocals) {
    EXPECT_EQ(interpret("local x = 1 local y = 2 local z = x + y"), INTERPRET_OK);
}

TEST_F(VMVariableTest, GlobalVariable) {
    // In Lua++, globals must be defined before use
    // This tests that assignment to undefined global fails
    EXPECT_EQ(interpret("x = 10"), INTERPRET_RUNTIME_ERROR);
}

TEST_F(VMVariableTest, GlobalAndLocal) {
    // Use local variables instead since globals need definition
    EXPECT_EQ(interpret("local x = 10 local y = x + 5"), INTERPRET_OK);
}

TEST_F(VMVariableTest, VariableReassignment) {
    EXPECT_EQ(interpret("local x = 1 x = 2 x = 3"), INTERPRET_OK);
}

TEST_F(VMVariableTest, ShadowingInScope) {
    EXPECT_EQ(interpret(R"(
        local x = 1
        do
            local x = 2
        end
    )"), INTERPRET_OK);
}

// ============== Control Flow Tests ==============

class VMControlFlowTest : public ::testing::Test {
protected:
    void SetUp() override { initVM(); }
    void TearDown() override { freeVM(); }
};

TEST_F(VMControlFlowTest, IfThen) {
    EXPECT_EQ(interpret("if true then local x = 1 end"), INTERPRET_OK);
}

TEST_F(VMControlFlowTest, IfThenElse) {
    EXPECT_EQ(interpret("if false then local x = 1 else local x = 2 end"), INTERPRET_OK);
}

TEST_F(VMControlFlowTest, IfElseif) {
    EXPECT_EQ(interpret(R"(
        local x = 2
        if x == 1 then
            local y = 1
        elseif x == 2 then
            local y = 2
        else
            local y = 3
        end
    )"), INTERPRET_OK);
}

TEST_F(VMControlFlowTest, WhileLoop) {
    EXPECT_EQ(interpret(R"(
        local i = 0
        while i < 5 do
            i = i + 1
        end
    )"), INTERPRET_OK);
}

TEST_F(VMControlFlowTest, ForLoop) {
    EXPECT_EQ(interpret("for i = 1, 3 do local x = i end"), INTERPRET_OK);
}

TEST_F(VMControlFlowTest, ForLoopWithStep) {
    EXPECT_EQ(interpret("for i = 1, 5, 2 do local x = i end"), INTERPRET_OK);
}

TEST_F(VMControlFlowTest, RepeatUntil) {
    EXPECT_EQ(interpret(R"(
        local i = 0
        repeat
            i = i + 1
        until i >= 5
    )"), INTERPRET_OK);
}

TEST_F(VMControlFlowTest, NestedLoops) {
    EXPECT_EQ(interpret(R"(
        for i = 1, 3 do
            for j = 1, 3 do
                local x = i * j
            end
        end
    )"), INTERPRET_OK);
}

TEST_F(VMControlFlowTest, DoBlock) {
    EXPECT_EQ(interpret(R"(
        do
            local x = 1
            local y = 2
        end
    )"), INTERPRET_OK);
}

// ============== Function Tests ==============

class VMFunctionTest : public ::testing::Test {
protected:
    void SetUp() override { initVM(); }
    void TearDown() override { freeVM(); }
};

TEST_F(VMFunctionTest, SimpleFunctionDeclaration) {
    EXPECT_EQ(interpret(R"(
        function foo()
        end
    )"), INTERPRET_OK);
}

TEST_F(VMFunctionTest, FunctionWithParameters) {
    EXPECT_EQ(interpret(R"(
        function add(a, b)
            return a + b
        end
    )"), INTERPRET_OK);
}

TEST_F(VMFunctionTest, FunctionCall) {
    EXPECT_EQ(interpret(R"(
        function double(x)
            return x * 2
        end
        local result = double(5)
    )"), INTERPRET_OK);
}

TEST_F(VMFunctionTest, LocalFunction) {
    EXPECT_EQ(interpret(R"(
        local function helper(x)
            return x + 1
        end
        local y = helper(10)
    )"), INTERPRET_OK);
}

TEST_F(VMFunctionTest, RecursiveFunction) {
    EXPECT_EQ(interpret(R"(
        function factorial(n)
            if n <= 1 then
                return 1
            end
            return n * factorial(n - 1)
        end
        local result = factorial(5)
    )"), INTERPRET_OK);
}

TEST_F(VMFunctionTest, MultipleReturnPaths) {
    EXPECT_EQ(interpret(R"(
        function abs(x)
            if x < 0 then
                return -x
            else
                return x
            end
        end
        local a = abs(-5)
        local b = abs(5)
    )"), INTERPRET_OK);
}

TEST_F(VMFunctionTest, NestedFunctions) {
    EXPECT_EQ(interpret(R"(
        function outer()
            function inner()
                return 42
            end
            return inner()
        end
        local x = outer()
    )"), INTERPRET_OK);
}

// ============== Closure Tests ==============

class VMClosureTest : public ::testing::Test {
protected:
    void SetUp() override { initVM(); }
    void TearDown() override { freeVM(); }
};

TEST_F(VMClosureTest, SimpleClosure) {
    EXPECT_EQ(interpret(R"(
        function makeAdder(x)
            function adder(y)
                return x + y
            end
            return adder
        end
        local add5 = makeAdder(5)
        local result = add5(3)
    )"), INTERPRET_OK);
}

TEST_F(VMClosureTest, ClosureWithMutation) {
    EXPECT_EQ(interpret(R"(
        function counter()
            local count = 0
            function increment()
                count = count + 1
                return count
            end
            return increment
        end
        local c = counter()
        local a = c()
        local b = c()
        local d = c()
    )"), INTERPRET_OK);
}

TEST_F(VMClosureTest, MultipleClosures) {
    EXPECT_EQ(interpret(R"(
        function makePair()
            local value = 0
            function get()
                return value
            end
            function set(v)
                value = v
            end
            return get
        end
        local g = makePair()
    )"), INTERPRET_OK);
}

TEST_F(VMClosureTest, DeepNesting) {
    EXPECT_EQ(interpret(R"(
        function level1()
            local a = 1
            function level2()
                local b = 2
                function level3()
                    return a + b
                end
                return level3
            end
            return level2
        end
        local f = level1()()
        local result = f()
    )"), INTERPRET_OK);
}

// ============== Error Handling Tests ==============

class VMErrorTest : public ::testing::Test {
protected:
    void SetUp() override { initVM(); }
    void TearDown() override { freeVM(); }
};

TEST_F(VMErrorTest, SyntaxError) {
    EXPECT_EQ(interpret("local x ="), INTERPRET_COMPILE_ERROR);
}

TEST_F(VMErrorTest, UndefinedVariable) {
    EXPECT_EQ(interpret("local x = undefined_var"), INTERPRET_RUNTIME_ERROR);
}

TEST_F(VMErrorTest, TypeErrorArithmetic) {
    EXPECT_EQ(interpret("local x = \"hello\" + 5"), INTERPRET_RUNTIME_ERROR);
}

TEST_F(VMErrorTest, TypeErrorComparison) {
    EXPECT_EQ(interpret("local x = \"hello\" < 5"), INTERPRET_RUNTIME_ERROR);
}

TEST_F(VMErrorTest, CallNonFunction) {
    EXPECT_EQ(interpret("local x = 5 x()"), INTERPRET_RUNTIME_ERROR);
}

TEST_F(VMErrorTest, UnterminatedString) {
    EXPECT_EQ(interpret("local x = \"unterminated"), INTERPRET_COMPILE_ERROR);
}

TEST_F(VMErrorTest, MissingEnd) {
    EXPECT_EQ(interpret("if true then local x = 1"), INTERPRET_COMPILE_ERROR);
}

TEST_F(VMErrorTest, MissingThen) {
    EXPECT_EQ(interpret("if true local x = 1 end"), INTERPRET_COMPILE_ERROR);
}

TEST_F(VMErrorTest, MissingDo) {
    EXPECT_EQ(interpret("while true local x = 1 end"), INTERPRET_COMPILE_ERROR);
}

// ============== Edge Cases & Stress Tests ==============

class VMEdgeCaseTest : public ::testing::Test {
protected:
    void SetUp() override { initVM(); }
    void TearDown() override { freeVM(); }
};

TEST_F(VMEdgeCaseTest, EmptyFunction) {
    EXPECT_EQ(interpret(R"(
        function empty()
        end
        empty()
    )"), INTERPRET_OK);
}

TEST_F(VMEdgeCaseTest, FunctionReturnsNil) {
    EXPECT_EQ(interpret(R"(
        function returnsNil()
        end
        local x = returnsNil()
    )"), INTERPRET_OK);
}

TEST_F(VMEdgeCaseTest, DeepRecursion) {
    // Test reasonable recursion depth
    EXPECT_EQ(interpret(R"(
        function recurse(n)
            if n <= 0 then
                return 0
            end
            return recurse(n - 1)
        end
        local x = recurse(50)
    )"), INTERPRET_OK);
}

TEST_F(VMEdgeCaseTest, ManyLocals) {
    // Test many local variables
    EXPECT_EQ(interpret(R"(
        local a1 = 1 local a2 = 2 local a3 = 3 local a4 = 4 local a5 = 5
        local a6 = 6 local a7 = 7 local a8 = 8 local a9 = 9 local a10 = 10
        local sum = a1 + a2 + a3 + a4 + a5 + a6 + a7 + a8 + a9 + a10
    )"), INTERPRET_OK);
}

TEST_F(VMEdgeCaseTest, LongStringLiteral) {
    std::string code = "local x = \"" + std::string(500, 'a') + "\"";
    EXPECT_EQ(interpret(code.c_str()), INTERPRET_OK);
}

TEST_F(VMEdgeCaseTest, NestedIfStatements) {
    EXPECT_EQ(interpret(R"(
        local x = 5
        if x > 0 then
            if x > 2 then
                if x > 4 then
                    local y = 1
                end
            end
        end
    )"), INTERPRET_OK);
}

TEST_F(VMEdgeCaseTest, ComplexExpression) {
    EXPECT_EQ(interpret(R"(
        local x = (1 + 2) * (3 - 4) / (5 + 6) % 7
    )"), INTERPRET_OK);
}

TEST_F(VMEdgeCaseTest, ShortCircuitAnd) {
    // Second part should not be evaluated
    EXPECT_EQ(interpret(R"(
        local x = false and undefined_var
    )"), INTERPRET_OK);
}

TEST_F(VMEdgeCaseTest, ShortCircuitOr) {
    // Second part should not be evaluated
    EXPECT_EQ(interpret(R"(
        local x = true or undefined_var
    )"), INTERPRET_OK);
}

TEST_F(VMEdgeCaseTest, ChainedComparisons) {
    EXPECT_EQ(interpret(R"(
        local a = 1 < 2
        local b = a == true
        local c = b and (3 > 2)
    )"), INTERPRET_OK);
}

TEST_F(VMEdgeCaseTest, StringWithSpecialChars) {
    EXPECT_EQ(interpret(R"(
        local x = "hello\nworld\ttab"
    )"), INTERPRET_OK);
}

TEST_F(VMEdgeCaseTest, EmptyWhileBody) {
    EXPECT_EQ(interpret(R"(
        local i = 0
        while i < 0 do
            i = i + 1
        end
    )"), INTERPRET_OK);
}

TEST_F(VMEdgeCaseTest, ZeroIterationFor) {
    EXPECT_EQ(interpret(R"(
        for i = 10, 1 do
            local x = i
        end
    )"), INTERPRET_OK);
}

// ============== Adversarial Input Tests ==============

class VMAdversarialTest : public ::testing::Test {
protected:
    void SetUp() override { initVM(); }
    void TearDown() override { freeVM(); }
};

TEST_F(VMAdversarialTest, KeywordAsPartOfIdentifier) {
    EXPECT_EQ(interpret("local endif = 1"), INTERPRET_OK);
    EXPECT_EQ(interpret("local dowhile = 2"), INTERPRET_OK);
    EXPECT_EQ(interpret("local returnvalue = 3"), INTERPRET_OK);
}

TEST_F(VMAdversarialTest, OperatorSpacing) {
    EXPECT_EQ(interpret("local x=1+2*3-4/2"), INTERPRET_OK);
}

TEST_F(VMAdversarialTest, ManyParentheses) {
    EXPECT_EQ(interpret("local x = ((((1 + 2))))"), INTERPRET_OK);
}

TEST_F(VMAdversarialTest, ConsecutiveStatements) {
    EXPECT_EQ(interpret("local a=1 local b=2 local c=3 local d=a+b+c"), INTERPRET_OK);
}

TEST_F(VMAdversarialTest, CommentInMiddle) {
    EXPECT_EQ(interpret(R"(
        local x = 1 -- comment
        local y = 2
    )"), INTERPRET_OK);
}

TEST_F(VMAdversarialTest, BlockCommentInExpression) {
    EXPECT_EQ(interpret("local x = 1 --[[ comment ]] + 2"), INTERPRET_OK);
}

TEST_F(VMAdversarialTest, EmptyBlockComment) {
    EXPECT_EQ(interpret("--[[]] local x = 1"), INTERPRET_OK);
}

TEST_F(VMAdversarialTest, NegativeNumbers) {
    EXPECT_EQ(interpret("local x = -1 local y = --1"), INTERPRET_COMPILE_ERROR);
}

TEST_F(VMAdversarialTest, DoubleNegation) {
    EXPECT_EQ(interpret("local x = - -1"), INTERPRET_OK);
}

TEST_F(VMAdversarialTest, NotNot) {
    EXPECT_EQ(interpret("local x = not not true"), INTERPRET_OK);
}
