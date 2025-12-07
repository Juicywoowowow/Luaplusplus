/*
 * test_compiler.cpp - Tests for Lua++ compiler/parser
 * 
 * Tests parsing and compilation of various language constructs,
 * focusing on syntax validation and error detection.
 */

#include <gtest/gtest.h>

extern "C" {
#include "compiler.h"
#include "vm.h"
}

// ============== Parser Syntax Tests ==============

class CompilerSyntaxTest : public ::testing::Test {
protected:
    void SetUp() override { initVM(); }
    void TearDown() override { freeVM(); }
    
    bool compiles(const char* source) {
        ObjFunction* fn = compile(source);
        return fn != nullptr;
    }
};

TEST_F(CompilerSyntaxTest, EmptySource) {
    EXPECT_TRUE(compiles(""));
}

TEST_F(CompilerSyntaxTest, ValidExpressions) {
    EXPECT_TRUE(compiles("local x = 1"));
    EXPECT_TRUE(compiles("local x = 1 + 2"));
    EXPECT_TRUE(compiles("local x = 1 + 2 * 3"));
    EXPECT_TRUE(compiles("local x = (1 + 2) * 3"));
    EXPECT_TRUE(compiles("local x = -1"));
    EXPECT_TRUE(compiles("local x = not true"));
}

TEST_F(CompilerSyntaxTest, ValidStatements) {
    EXPECT_TRUE(compiles("if true then end"));
    EXPECT_TRUE(compiles("while true do end"));
    EXPECT_TRUE(compiles("for i = 1, 10 do end"));
    EXPECT_TRUE(compiles("repeat until true"));
    EXPECT_TRUE(compiles("do end"));
}

TEST_F(CompilerSyntaxTest, ValidFunctions) {
    EXPECT_TRUE(compiles("function foo() end"));
    EXPECT_TRUE(compiles("function foo(a) end"));
    EXPECT_TRUE(compiles("function foo(a, b, c) end"));
    EXPECT_TRUE(compiles("local function bar() end"));
}

TEST_F(CompilerSyntaxTest, ValidClasses) {
    EXPECT_TRUE(compiles("class Foo end"));
    EXPECT_TRUE(compiles("class Foo function bar() end end"));
    EXPECT_TRUE(compiles("class Child extends Parent end"));
}

// ============== Parser Error Tests ==============

class CompilerErrorTest : public ::testing::Test {
protected:
    void SetUp() override { initVM(); }
    void TearDown() override { freeVM(); }
    
    bool compiles(const char* source) {
        ObjFunction* fn = compile(source);
        return fn != nullptr;
    }
};

TEST_F(CompilerErrorTest, MissingEnd) {
    EXPECT_FALSE(compiles("if true then"));
    EXPECT_FALSE(compiles("while true do"));
    EXPECT_FALSE(compiles("for i = 1, 10 do"));
    EXPECT_FALSE(compiles("function foo()"));
    EXPECT_FALSE(compiles("class Foo"));
    EXPECT_FALSE(compiles("do"));
}

TEST_F(CompilerErrorTest, MissingThen) {
    EXPECT_FALSE(compiles("if true end"));
    EXPECT_FALSE(compiles("if true local x = 1 end"));
}

TEST_F(CompilerErrorTest, MissingDo) {
    EXPECT_FALSE(compiles("while true end"));
    EXPECT_FALSE(compiles("for i = 1, 10 end"));
}

TEST_F(CompilerErrorTest, MissingUntil) {
    EXPECT_FALSE(compiles("repeat"));
    EXPECT_FALSE(compiles("repeat local x = 1"));
}

TEST_F(CompilerErrorTest, UnbalancedParentheses) {
    EXPECT_FALSE(compiles("local x = (1 + 2"));
    EXPECT_FALSE(compiles("local x = 1 + 2)"));
    EXPECT_FALSE(compiles("local x = ((1 + 2)"));
}

TEST_F(CompilerErrorTest, InvalidAssignment) {
    EXPECT_FALSE(compiles("1 = 2"));
    EXPECT_FALSE(compiles("true = false"));
    EXPECT_FALSE(compiles("\"str\" = 1"));
}

TEST_F(CompilerErrorTest, MissingExpression) {
    EXPECT_FALSE(compiles("local x ="));
    EXPECT_FALSE(compiles("local x = +"));
    EXPECT_FALSE(compiles("if then end"));
}

TEST_F(CompilerErrorTest, InvalidFunctionSyntax) {
    EXPECT_FALSE(compiles("function () end"));  // Missing name
    EXPECT_FALSE(compiles("function foo( end"));  // Missing )
    EXPECT_FALSE(compiles("function foo) end"));  // Missing (
}

TEST_F(CompilerErrorTest, InvalidClassSyntax) {
    EXPECT_FALSE(compiles("class end"));  // Missing name
    EXPECT_FALSE(compiles("class extends Parent end"));  // Missing name
    EXPECT_FALSE(compiles("class Foo extends end"));  // Missing parent
}

TEST_F(CompilerErrorTest, InvalidForSyntax) {
    EXPECT_FALSE(compiles("for = 1, 10 do end"));  // Missing var
    EXPECT_FALSE(compiles("for i 1, 10 do end"));  // Missing =
    EXPECT_FALSE(compiles("for i = , 10 do end"));  // Missing start
    EXPECT_FALSE(compiles("for i = 1 10 do end"));  // Missing comma
}

TEST_F(CompilerErrorTest, ReturnOutsideFunction) {
    EXPECT_FALSE(compiles("return 1"));
}

// ============== Precedence Tests ==============

class CompilerPrecedenceTest : public ::testing::Test {
protected:
    void SetUp() override { initVM(); }
    void TearDown() override { freeVM(); }
    
    bool compiles(const char* source) {
        ObjFunction* fn = compile(source);
        return fn != nullptr;
    }
};

TEST_F(CompilerPrecedenceTest, ArithmeticPrecedence) {
    // These should all compile - testing that precedence parsing works
    EXPECT_TRUE(compiles("local x = 1 + 2 * 3"));
    EXPECT_TRUE(compiles("local x = 1 * 2 + 3"));
    EXPECT_TRUE(compiles("local x = 1 + 2 + 3"));
    EXPECT_TRUE(compiles("local x = 1 - 2 - 3"));
    EXPECT_TRUE(compiles("local x = 1 * 2 / 3"));
    EXPECT_TRUE(compiles("local x = 1 / 2 * 3"));
}

TEST_F(CompilerPrecedenceTest, ComparisonPrecedence) {
    EXPECT_TRUE(compiles("local x = 1 + 2 < 3 + 4"));
    EXPECT_TRUE(compiles("local x = 1 < 2 == true"));
    EXPECT_TRUE(compiles("local x = 1 == 2 and 3 == 4"));
}

TEST_F(CompilerPrecedenceTest, LogicalPrecedence) {
    EXPECT_TRUE(compiles("local x = true and false or true"));
    EXPECT_TRUE(compiles("local x = true or false and true"));
    EXPECT_TRUE(compiles("local x = not true and false"));
}

TEST_F(CompilerPrecedenceTest, UnaryPrecedence) {
    EXPECT_TRUE(compiles("local x = -1 + 2"));
    EXPECT_TRUE(compiles("local x = 1 + -2"));
    EXPECT_TRUE(compiles("local x = not true and false"));
    EXPECT_TRUE(compiles("local x = - - 1"));
}

TEST_F(CompilerPrecedenceTest, ConcatPrecedence) {
    EXPECT_TRUE(compiles("local x = \"a\" .. \"b\" .. \"c\""));
    EXPECT_TRUE(compiles("local x = 1 + 2 .. 3 + 4"));  // Concat has lower precedence
}

// ============== Scope Tests ==============

class CompilerScopeTest : public ::testing::Test {
protected:
    void SetUp() override { initVM(); }
    void TearDown() override { freeVM(); }
    
    bool compiles(const char* source) {
        ObjFunction* fn = compile(source);
        return fn != nullptr;
    }
};

TEST_F(CompilerScopeTest, LocalScoping) {
    EXPECT_TRUE(compiles(R"(
        local x = 1
        do
            local x = 2
        end
    )"));
}

TEST_F(CompilerScopeTest, NestedScopes) {
    EXPECT_TRUE(compiles(R"(
        do
            local a = 1
            do
                local b = 2
                do
                    local c = 3
                end
            end
        end
    )"));
}

TEST_F(CompilerScopeTest, FunctionScope) {
    EXPECT_TRUE(compiles(R"(
        local x = 1
        function foo()
            local x = 2
            return x
        end
    )"));
}

TEST_F(CompilerScopeTest, ClosureCapture) {
    EXPECT_TRUE(compiles(R"(
        function outer()
            local x = 1
            function inner()
                return x
            end
            return inner
        end
    )"));
}

TEST_F(CompilerScopeTest, RedeclarationInSameScope) {
    // This should fail - redeclaring in same scope
    EXPECT_FALSE(compiles(R"(
        do
            local x = 1
            local x = 2
        end
    )"));
}

// ============== Edge Case Syntax Tests ==============

class CompilerEdgeCaseTest : public ::testing::Test {
protected:
    void SetUp() override { initVM(); }
    void TearDown() override { freeVM(); }
    
    bool compiles(const char* source) {
        ObjFunction* fn = compile(source);
        return fn != nullptr;
    }
};

TEST_F(CompilerEdgeCaseTest, EmptyBlocks) {
    EXPECT_TRUE(compiles("if true then end"));
    EXPECT_TRUE(compiles("while false do end"));
    EXPECT_TRUE(compiles("for i = 1, 0 do end"));
    EXPECT_TRUE(compiles("function foo() end"));
    EXPECT_TRUE(compiles("do end"));
}

TEST_F(CompilerEdgeCaseTest, NestedControlFlow) {
    EXPECT_TRUE(compiles(R"(
        if true then
            if true then
                while true do
                    for i = 1, 10 do
                    end
                end
            end
        end
    )"));
}

TEST_F(CompilerEdgeCaseTest, ElseifChain) {
    EXPECT_TRUE(compiles(R"(
        local x = 1
        if x == 1 then
        elseif x == 2 then
        elseif x == 3 then
        elseif x == 4 then
        else
        end
    )"));
}

TEST_F(CompilerEdgeCaseTest, ComplexExpressions) {
    EXPECT_TRUE(compiles("local x = ((((1))))"));
    EXPECT_TRUE(compiles("local x = 1 + 2 + 3 + 4 + 5 + 6 + 7 + 8 + 9 + 10"));
    EXPECT_TRUE(compiles("local x = 1 * 2 * 3 * 4 * 5"));
    EXPECT_TRUE(compiles("local x = true and true and true and true"));
}

TEST_F(CompilerEdgeCaseTest, LongIdentifiers) {
    EXPECT_TRUE(compiles("local thisIsAVeryLongVariableNameThatShouldStillWork = 1"));
}

TEST_F(CompilerEdgeCaseTest, ManyParameters) {
    EXPECT_TRUE(compiles("function foo(a,b,c,d,e,f,g,h,i,j) end"));
}

TEST_F(CompilerEdgeCaseTest, ManyArguments) {
    EXPECT_TRUE(compiles(R"(
        function foo(a,b,c,d,e,f,g,h,i,j)
        end
        foo(1,2,3,4,5,6,7,8,9,10)
    )"));
}

TEST_F(CompilerEdgeCaseTest, ChainedMethodCalls) {
    EXPECT_TRUE(compiles(R"(
        class Builder
            function a() return self end
            function b() return self end
            function c() return self end
        end
        local b = new Builder()
        b:a():b():c()
    )"));
}

TEST_F(CompilerEdgeCaseTest, ChainedPropertyAccess) {
    EXPECT_TRUE(compiles(R"(
        class A
            function init()
                self.b = new B()
            end
        end
        class B
            function init()
                self.c = new C()
            end
        end
        class C
            function init()
                self.value = 42
            end
        end
        local a = new A()
        local v = a.b.c.value
    )"));
}

// ============== Adversarial Syntax Tests ==============

class CompilerAdversarialTest : public ::testing::Test {
protected:
    void SetUp() override { initVM(); }
    void TearDown() override { freeVM(); }
    
    bool compiles(const char* source) {
        ObjFunction* fn = compile(source);
        return fn != nullptr;
    }
};

TEST_F(CompilerAdversarialTest, KeywordsInStrings) {
    EXPECT_TRUE(compiles("local x = \"if then else end while do for\""));
}

TEST_F(CompilerAdversarialTest, KeywordsInComments) {
    EXPECT_TRUE(compiles("-- if then else end\nlocal x = 1"));
    EXPECT_TRUE(compiles("--[[ if then else end ]] local x = 1"));
}

TEST_F(CompilerAdversarialTest, AlmostKeywords) {
    EXPECT_TRUE(compiles("local iff = 1"));
    EXPECT_TRUE(compiles("local thenn = 1"));
    EXPECT_TRUE(compiles("local endd = 1"));
    EXPECT_TRUE(compiles("local classs = 1"));
}

TEST_F(CompilerAdversarialTest, OperatorsWithoutSpaces) {
    EXPECT_TRUE(compiles("local x=1+2*3-4/5%6"));
}

TEST_F(CompilerAdversarialTest, ManyStatements) {
    std::string code;
    for (int i = 0; i < 100; i++) {
        code += "local x" + std::to_string(i) + " = " + std::to_string(i) + "\n";
    }
    EXPECT_TRUE(compiles(code.c_str()));
}

TEST_F(CompilerAdversarialTest, DeeplyNestedExpressions) {
    std::string code = "local x = ";
    for (int i = 0; i < 20; i++) code += "(";
    code += "1";
    for (int i = 0; i < 20; i++) code += ")";
    EXPECT_TRUE(compiles(code.c_str()));
}

TEST_F(CompilerAdversarialTest, ConsecutiveOperators) {
    EXPECT_TRUE(compiles("local x = 1 + - 2"));
    EXPECT_TRUE(compiles("local x = - - 1"));
    EXPECT_TRUE(compiles("local x = not not true"));
}

TEST_F(CompilerAdversarialTest, EmptyStringLiteral) {
    EXPECT_TRUE(compiles("local x = \"\""));
    EXPECT_TRUE(compiles("local x = ''"));
}

TEST_F(CompilerAdversarialTest, StringWithEscapes) {
    EXPECT_TRUE(compiles("local x = \"\\n\\t\\r\\\\\\\"\""));
}
