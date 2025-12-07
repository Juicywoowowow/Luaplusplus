/*
 * test_oop.cpp - Tests for Lua++ OOP features
 * 
 * Tests class declarations, inheritance, methods, constructors,
 * self/super keywords, and private members.
 */

#include <gtest/gtest.h>

extern "C" {
#include "vm.h"
#include "compiler.h"
}

// ============== Class Declaration Tests ==============

class OOPClassTest : public ::testing::Test {
protected:
    void SetUp() override { initVM(); }
    void TearDown() override { freeVM(); }
};

TEST_F(OOPClassTest, EmptyClass) {
    EXPECT_EQ(interpret(R"(
        class Empty
        end
    )"), INTERPRET_OK);
}

TEST_F(OOPClassTest, ClassWithMethod) {
    EXPECT_EQ(interpret(R"(
        class Greeter
            function greet()
                return "hello"
            end
        end
    )"), INTERPRET_OK);
}

TEST_F(OOPClassTest, ClassWithMultipleMethods) {
    EXPECT_EQ(interpret(R"(
        class Calculator
            function add(a, b)
                return a + b
            end
            function subtract(a, b)
                return a - b
            end
            function multiply(a, b)
                return a * b
            end
        end
    )"), INTERPRET_OK);
}

TEST_F(OOPClassTest, ClassWithInit) {
    EXPECT_EQ(interpret(R"(
        class Person
            function init(name)
                self.name = name
            end
        end
    )"), INTERPRET_OK);
}

// ============== Instance Tests ==============

class OOPInstanceTest : public ::testing::Test {
protected:
    void SetUp() override { initVM(); }
    void TearDown() override { freeVM(); }
};

TEST_F(OOPInstanceTest, CreateInstance) {
    EXPECT_EQ(interpret(R"(
        class Point
        end
        local p = new Point()
    )"), INTERPRET_OK);
}

TEST_F(OOPInstanceTest, InstanceWithInit) {
    EXPECT_EQ(interpret(R"(
        class Point
            function init(x, y)
                self.x = x
                self.y = y
            end
        end
        local p = new Point(10, 20)
    )"), INTERPRET_OK);
}

TEST_F(OOPInstanceTest, AccessField) {
    EXPECT_EQ(interpret(R"(
        class Box
            function init(value)
                self.value = value
            end
        end
        local b = new Box(42)
        local v = b.value
    )"), INTERPRET_OK);
}

TEST_F(OOPInstanceTest, SetField) {
    EXPECT_EQ(interpret(R"(
        class Box
            function init(value)
                self.value = value
            end
        end
        local b = new Box(1)
        b.value = 2
    )"), INTERPRET_OK);
}

TEST_F(OOPInstanceTest, CallMethod) {
    EXPECT_EQ(interpret(R"(
        class Counter
            function init()
                self.count = 0
            end
            function increment()
                self.count = self.count + 1
            end
            function get()
                return self.count
            end
        end
        local c = new Counter()
        c:increment()
        c:increment()
        local val = c:get()
    )"), INTERPRET_OK);
}

TEST_F(OOPInstanceTest, MethodWithParameters) {
    EXPECT_EQ(interpret(R"(
        class Adder
            function init(base)
                self.base = base
            end
            function add(x)
                return self.base + x
            end
        end
        local a = new Adder(10)
        local result = a:add(5)
    )"), INTERPRET_OK);
}

TEST_F(OOPInstanceTest, ChainedMethodCalls) {
    EXPECT_EQ(interpret(R"(
        class Builder
            function init()
                self.value = 0
            end
            function add(x)
                self.value = self.value + x
                return self
            end
            function get()
                return self.value
            end
        end
        local b = new Builder()
        local result = b:add(1):add(2):add(3):get()
    )"), INTERPRET_OK);
}

// ============== Inheritance Tests ==============

class OOPInheritanceTest : public ::testing::Test {
protected:
    void SetUp() override { initVM(); }
    void TearDown() override { freeVM(); }
};

TEST_F(OOPInheritanceTest, SimpleInheritance) {
    EXPECT_EQ(interpret(R"(
        class Animal
        end
        class Dog extends Animal
        end
    )"), INTERPRET_OK);
}

TEST_F(OOPInheritanceTest, InheritMethod) {
    EXPECT_EQ(interpret(R"(
        class Animal
            function speak()
                return "sound"
            end
        end
        class Dog extends Animal
        end
        local d = new Dog()
        local s = d:speak()
    )"), INTERPRET_OK);
}

TEST_F(OOPInheritanceTest, OverrideMethod) {
    EXPECT_EQ(interpret(R"(
        class Animal
            function speak()
                return "generic sound"
            end
        end
        class Dog extends Animal
            function speak()
                return "bark"
            end
        end
        local d = new Dog()
        local s = d:speak()
    )"), INTERPRET_OK);
}

TEST_F(OOPInheritanceTest, SuperCall) {
    EXPECT_EQ(interpret(R"(
        class Animal
            function init(name)
                self.name = name
            end
        end
        class Dog extends Animal
            function init(name, breed)
                super.init(name)
                self.breed = breed
            end
        end
        local d = new Dog("Rex", "German Shepherd")
    )"), INTERPRET_OK);
}

TEST_F(OOPInheritanceTest, SuperMethodCall) {
    EXPECT_EQ(interpret(R"(
        class Animal
            function describe()
                return "an animal"
            end
        end
        class Dog extends Animal
            function describe()
                return super.describe() .. " that barks"
            end
        end
        local d = new Dog()
        local desc = d:describe()
    )"), INTERPRET_OK);
}

TEST_F(OOPInheritanceTest, MultiLevelInheritance) {
    EXPECT_EQ(interpret(R"(
        class A
            function foo()
                return 1
            end
        end
        class B extends A
            function bar()
                return 2
            end
        end
        class C extends B
            function baz()
                return 3
            end
        end
        local c = new C()
        local a = c:foo()
        local b = c:bar()
        local d = c:baz()
    )"), INTERPRET_OK);
}

TEST_F(OOPInheritanceTest, InheritedFieldAccess) {
    EXPECT_EQ(interpret(R"(
        class Base
            function init()
                self.baseField = 100
            end
        end
        class Derived extends Base
            function init()
                super.init()
                self.derivedField = 200
            end
            function getBase()
                return self.baseField
            end
        end
        local d = new Derived()
        local val = d:getBase()
    )"), INTERPRET_OK);
}

// ============== Self Keyword Tests ==============

class OOPSelfTest : public ::testing::Test {
protected:
    void SetUp() override { initVM(); }
    void TearDown() override { freeVM(); }
};

TEST_F(OOPSelfTest, SelfInMethod) {
    EXPECT_EQ(interpret(R"(
        class Box
            function init(v)
                self.value = v
            end
            function getValue()
                return self.value
            end
        end
        local b = new Box(42)
        local v = b:getValue()
    )"), INTERPRET_OK);
}

TEST_F(OOPSelfTest, SelfMethodCall) {
    EXPECT_EQ(interpret(R"(
        class Calculator
            function double(x)
                return x * 2
            end
            function quadruple(x)
                return self:double(self:double(x))
            end
        end
        local c = new Calculator()
        local result = c:quadruple(5)
    )"), INTERPRET_OK);
}

TEST_F(OOPSelfTest, SelfOutsideClassError) {
    EXPECT_EQ(interpret(R"(
        function foo()
            return self.x
        end
    )"), INTERPRET_COMPILE_ERROR);
}

// ============== Super Keyword Tests ==============

class OOPSuperTest : public ::testing::Test {
protected:
    void SetUp() override { initVM(); }
    void TearDown() override { freeVM(); }
};

TEST_F(OOPSuperTest, SuperInInit) {
    EXPECT_EQ(interpret(R"(
        class Parent
            function init(x)
                self.x = x
            end
        end
        class Child extends Parent
            function init(x, y)
                super.init(x)
                self.y = y
            end
        end
        local c = new Child(1, 2)
    )"), INTERPRET_OK);
}

TEST_F(OOPSuperTest, SuperMethodChain) {
    EXPECT_EQ(interpret(R"(
        class A
            function foo()
                return 1
            end
        end
        class B extends A
            function foo()
                return super.foo() + 1
            end
        end
        class C extends B
            function foo()
                return super.foo() + 1
            end
        end
        local c = new C()
        local result = c:foo()
    )"), INTERPRET_OK);
}

TEST_F(OOPSuperTest, SuperOutsideClassError) {
    EXPECT_EQ(interpret(R"(
        function foo()
            return super.bar()
        end
    )"), INTERPRET_COMPILE_ERROR);
}

TEST_F(OOPSuperTest, SuperWithoutSuperclassError) {
    EXPECT_EQ(interpret(R"(
        class NoParent
            function foo()
                return super.bar()
            end
        end
    )"), INTERPRET_COMPILE_ERROR);
}

// ============== Private Members Tests ==============

class OOPPrivateTest : public ::testing::Test {
protected:
    void SetUp() override { initVM(); }
    void TearDown() override { freeVM(); }
};

TEST_F(OOPPrivateTest, PrivateMethod) {
    EXPECT_EQ(interpret(R"(
        class Secret
            private function hidden()
                return 42
            end
            function reveal()
                return self:hidden()
            end
        end
        local s = new Secret()
        local v = s:reveal()
    )"), INTERPRET_OK);
}

// ============== OOP Edge Cases ==============

class OOPEdgeCaseTest : public ::testing::Test {
protected:
    void SetUp() override { initVM(); }
    void TearDown() override { freeVM(); }
};

TEST_F(OOPEdgeCaseTest, EmptyInit) {
    EXPECT_EQ(interpret(R"(
        class Empty
            function init()
            end
        end
        local e = new Empty()
    )"), INTERPRET_OK);
}

TEST_F(OOPEdgeCaseTest, InitReturnsImplicitly) {
    EXPECT_EQ(interpret(R"(
        class Test
            function init()
                self.x = 1
            end
        end
        local t = new Test()
    )"), INTERPRET_OK);
}

TEST_F(OOPEdgeCaseTest, MethodSameName) {
    EXPECT_EQ(interpret(R"(
        class A
            function foo()
                return 1
            end
        end
        class B
            function foo()
                return 2
            end
        end
        local a = new A()
        local b = new B()
        local x = a:foo()
        local y = b:foo()
    )"), INTERPRET_OK);
}

TEST_F(OOPEdgeCaseTest, InstanceAsField) {
    EXPECT_EQ(interpret(R"(
        class Inner
            function init(v)
                self.value = v
            end
        end
        class Outer
            function init()
                self.inner = new Inner(42)
            end
            function getInnerValue()
                return self.inner.value
            end
        end
        local o = new Outer()
        local v = o:getInnerValue()
    )"), INTERPRET_OK);
}

TEST_F(OOPEdgeCaseTest, ClassAsGlobal) {
    EXPECT_EQ(interpret(R"(
        class MyClass
            function init(x)
                self.x = x
            end
        end
        local instance = new MyClass(10)
    )"), INTERPRET_OK);
}

TEST_F(OOPEdgeCaseTest, MultipleInstances) {
    EXPECT_EQ(interpret(R"(
        class Counter
            function init(start)
                self.value = start
            end
            function inc()
                self.value = self.value + 1
            end
        end
        local c1 = new Counter(0)
        local c2 = new Counter(100)
        c1:inc()
        c2:inc()
    )"), INTERPRET_OK);
}

TEST_F(OOPEdgeCaseTest, MethodReturnsInstance) {
    EXPECT_EQ(interpret(R"(
        class Factory
            function create(x)
                return new Product(x)
            end
        end
        class Product
            function init(v)
                self.value = v
            end
        end
        local f = new Factory()
        local p = f:create(42)
    )"), INTERPRET_OK);
}

// ============== OOP Error Cases ==============

class OOPErrorTest : public ::testing::Test {
protected:
    void SetUp() override { initVM(); }
    void TearDown() override { freeVM(); }
};

TEST_F(OOPErrorTest, NewNonClass) {
    EXPECT_EQ(interpret(R"(
        local x = 42
        local y = new x()
    )"), INTERPRET_RUNTIME_ERROR);
}

TEST_F(OOPErrorTest, AccessFieldOnNonInstance) {
    EXPECT_EQ(interpret(R"(
        local x = 42
        local y = x.field
    )"), INTERPRET_RUNTIME_ERROR);
}

TEST_F(OOPErrorTest, SetFieldOnNonInstance) {
    EXPECT_EQ(interpret(R"(
        local x = 42
        x.field = 1
    )"), INTERPRET_RUNTIME_ERROR);
}

TEST_F(OOPErrorTest, UndefinedMethod) {
    EXPECT_EQ(interpret(R"(
        class Empty
        end
        local e = new Empty()
        e:nonexistent()
    )"), INTERPRET_RUNTIME_ERROR);
}

TEST_F(OOPErrorTest, InheritFromNonClass) {
    EXPECT_EQ(interpret(R"(
        local x = 42
        class Bad extends x
        end
    )"), INTERPRET_RUNTIME_ERROR);
}

TEST_F(OOPErrorTest, SelfInheritance) {
    EXPECT_EQ(interpret(R"(
        class Recursive extends Recursive
        end
    )"), INTERPRET_COMPILE_ERROR);
}

// ============== Complex OOP Scenarios ==============

class OOPComplexTest : public ::testing::Test {
protected:
    void SetUp() override { initVM(); }
    void TearDown() override { freeVM(); }
};

TEST_F(OOPComplexTest, PolymorphicBehavior) {
    EXPECT_EQ(interpret(R"(
        class Shape
            function area()
                return 0
            end
        end
        class Rectangle extends Shape
            function init(w, h)
                self.width = w
                self.height = h
            end
            function area()
                return self.width * self.height
            end
        end
        class Circle extends Shape
            function init(r)
                self.radius = r
            end
            function area()
                return 3.14159 * self.radius * self.radius
            end
        end
        local rect = new Rectangle(10, 5)
        local circ = new Circle(7)
        local a1 = rect:area()
        local a2 = circ:area()
    )"), INTERPRET_OK);
}

TEST_F(OOPComplexTest, FactoryPattern) {
    EXPECT_EQ(interpret(R"(
        class Animal
            function speak()
                return "..."
            end
        end
        class Dog extends Animal
            function speak()
                return "woof"
            end
        end
        class Cat extends Animal
            function speak()
                return "meow"
            end
        end
        class AnimalFactory
            function createDog()
                return new Dog()
            end
            function createCat()
                return new Cat()
            end
        end
        local factory = new AnimalFactory()
        local dog = factory:createDog()
        local cat = factory:createCat()
        local s1 = dog:speak()
        local s2 = cat:speak()
    )"), INTERPRET_OK);
}

TEST_F(OOPComplexTest, CompositionOverInheritance) {
    EXPECT_EQ(interpret(R"(
        class Engine
            function init(hp)
                self.horsepower = hp
            end
            function start()
                return "vroom"
            end
        end
        class Car
            function init(hp)
                self.engine = new Engine(hp)
            end
            function start()
                return self.engine:start()
            end
            function getHP()
                return self.engine.horsepower
            end
        end
        local car = new Car(200)
        local sound = car:start()
        local hp = car:getHP()
    )"), INTERPRET_OK);
}

TEST_F(OOPComplexTest, MethodChaining) {
    EXPECT_EQ(interpret(R"(
        class StringBuilder
            function init()
                self.str = ""
            end
            function append(s)
                self.str = self.str .. s
                return self
            end
            function build()
                return self.str
            end
        end
        local sb = new StringBuilder()
        local result = sb:append("Hello"):append(" "):append("World"):build()
    )"), INTERPRET_OK);
}

TEST_F(OOPComplexTest, RecursiveDataStructure) {
    EXPECT_EQ(interpret(R"(
        class Node
            function init(value)
                self.value = value
                self.next = nil
            end
            function setNext(node)
                self.next = node
            end
            function getValue()
                return self.value
            end
        end
        local n1 = new Node(1)
        local n2 = new Node(2)
        local n3 = new Node(3)
        n1:setNext(n2)
        n2:setNext(n3)
    )"), INTERPRET_OK);
}
