/*
 * test_value.cpp - Tests for Lua++ value system
 * 
 * Tests Value creation, type checking, equality, and ValueArray operations.
 */

#include <gtest/gtest.h>

extern "C" {
#include "value.h"
#include "object.h"
#include "vm.h"
}

// ============== Value Creation Tests ==============

class ValueCreationTest : public ::testing::Test {
protected:
    void SetUp() override {
        initVM();
    }
    void TearDown() override {
        freeVM();
    }
};

TEST_F(ValueCreationTest, NilValue) {
    Value v = NIL_VAL;
    EXPECT_TRUE(IS_NIL(v));
    EXPECT_FALSE(IS_BOOL(v));
    EXPECT_FALSE(IS_NUMBER(v));
    EXPECT_FALSE(IS_OBJ(v));
}

TEST_F(ValueCreationTest, BoolValues) {
    Value t = BOOL_VAL(true);
    Value f = BOOL_VAL(false);
    
    EXPECT_TRUE(IS_BOOL(t));
    EXPECT_TRUE(IS_BOOL(f));
    EXPECT_TRUE(AS_BOOL(t));
    EXPECT_FALSE(AS_BOOL(f));
}

TEST_F(ValueCreationTest, NumberValues) {
    Value zero = NUMBER_VAL(0.0);
    Value pi = NUMBER_VAL(3.14159);
    Value negative = NUMBER_VAL(-42.5);
    
    EXPECT_TRUE(IS_NUMBER(zero));
    EXPECT_TRUE(IS_NUMBER(pi));
    EXPECT_TRUE(IS_NUMBER(negative));
    
    EXPECT_DOUBLE_EQ(AS_NUMBER(zero), 0.0);
    EXPECT_DOUBLE_EQ(AS_NUMBER(pi), 3.14159);
    EXPECT_DOUBLE_EQ(AS_NUMBER(negative), -42.5);
}

TEST_F(ValueCreationTest, StringValues) {
    ObjString* str = copyString("hello", 5);
    Value v = OBJ_VAL(str);
    
    EXPECT_TRUE(IS_OBJ(v));
    EXPECT_TRUE(IS_STRING(v));
    EXPECT_STREQ(AS_CSTRING(v), "hello");
}

TEST_F(ValueCreationTest, EmptyString) {
    ObjString* str = copyString("", 0);
    Value v = OBJ_VAL(str);
    
    EXPECT_TRUE(IS_STRING(v));
    EXPECT_EQ(AS_STRING(v)->length, 0);
    EXPECT_STREQ(AS_CSTRING(v), "");
}

// ============== Value Equality Tests ==============

class ValueEqualityTest : public ::testing::Test {
protected:
    void SetUp() override {
        initVM();
    }
    void TearDown() override {
        freeVM();
    }
};

TEST_F(ValueEqualityTest, NilEquality) {
    EXPECT_TRUE(valuesEqual(NIL_VAL, NIL_VAL));
}

TEST_F(ValueEqualityTest, BoolEquality) {
    EXPECT_TRUE(valuesEqual(BOOL_VAL(true), BOOL_VAL(true)));
    EXPECT_TRUE(valuesEqual(BOOL_VAL(false), BOOL_VAL(false)));
    EXPECT_FALSE(valuesEqual(BOOL_VAL(true), BOOL_VAL(false)));
}

TEST_F(ValueEqualityTest, NumberEquality) {
    EXPECT_TRUE(valuesEqual(NUMBER_VAL(42), NUMBER_VAL(42)));
    EXPECT_TRUE(valuesEqual(NUMBER_VAL(0), NUMBER_VAL(0)));
    EXPECT_TRUE(valuesEqual(NUMBER_VAL(-1.5), NUMBER_VAL(-1.5)));
    EXPECT_FALSE(valuesEqual(NUMBER_VAL(1), NUMBER_VAL(2)));
}

TEST_F(ValueEqualityTest, StringEquality) {
    ObjString* s1 = copyString("hello", 5);
    ObjString* s2 = copyString("hello", 5);
    ObjString* s3 = copyString("world", 5);
    
    // Interned strings should be same pointer
    EXPECT_EQ(s1, s2);
    EXPECT_TRUE(valuesEqual(OBJ_VAL(s1), OBJ_VAL(s2)));
    EXPECT_FALSE(valuesEqual(OBJ_VAL(s1), OBJ_VAL(s3)));
}

TEST_F(ValueEqualityTest, DifferentTypesNotEqual) {
    EXPECT_FALSE(valuesEqual(NIL_VAL, BOOL_VAL(false)));
    EXPECT_FALSE(valuesEqual(NIL_VAL, NUMBER_VAL(0)));
    EXPECT_FALSE(valuesEqual(BOOL_VAL(false), NUMBER_VAL(0)));
    EXPECT_FALSE(valuesEqual(NUMBER_VAL(0), BOOL_VAL(false)));
}

TEST_F(ValueEqualityTest, NumberEdgeCases) {
    // Very small numbers
    EXPECT_TRUE(valuesEqual(NUMBER_VAL(0.0000001), NUMBER_VAL(0.0000001)));
    
    // Very large numbers
    EXPECT_TRUE(valuesEqual(NUMBER_VAL(1e308), NUMBER_VAL(1e308)));
    
    // Negative zero
    EXPECT_TRUE(valuesEqual(NUMBER_VAL(0.0), NUMBER_VAL(-0.0)));
}

// ============== ValueArray Tests ==============

class ValueArrayTest : public ::testing::Test {
protected:
    ValueArray array;
    
    void SetUp() override {
        initVM();
        initValueArray(&array);
    }
    void TearDown() override {
        freeValueArray(&array);
        freeVM();
    }
};

TEST_F(ValueArrayTest, InitialState) {
    EXPECT_EQ(array.count, 0);
    EXPECT_EQ(array.capacity, 0);
    EXPECT_EQ(array.values, nullptr);
}

TEST_F(ValueArrayTest, WriteAndRead) {
    writeValueArray(&array, NUMBER_VAL(1));
    writeValueArray(&array, NUMBER_VAL(2));
    writeValueArray(&array, NUMBER_VAL(3));
    
    EXPECT_EQ(array.count, 3);
    EXPECT_DOUBLE_EQ(AS_NUMBER(array.values[0]), 1);
    EXPECT_DOUBLE_EQ(AS_NUMBER(array.values[1]), 2);
    EXPECT_DOUBLE_EQ(AS_NUMBER(array.values[2]), 3);
}

TEST_F(ValueArrayTest, MixedTypes) {
    writeValueArray(&array, NIL_VAL);
    writeValueArray(&array, BOOL_VAL(true));
    writeValueArray(&array, NUMBER_VAL(42));
    
    EXPECT_TRUE(IS_NIL(array.values[0]));
    EXPECT_TRUE(IS_BOOL(array.values[1]));
    EXPECT_TRUE(IS_NUMBER(array.values[2]));
}

TEST_F(ValueArrayTest, GrowsAutomatically) {
    for (int i = 0; i < 100; i++) {
        writeValueArray(&array, NUMBER_VAL((double)i));
    }
    
    EXPECT_EQ(array.count, 100);
    EXPECT_GE(array.capacity, 100);
    
    // Verify all values
    for (int i = 0; i < 100; i++) {
        EXPECT_DOUBLE_EQ(AS_NUMBER(array.values[i]), (double)i);
    }
}

TEST_F(ValueArrayTest, FreeResetsState) {
    writeValueArray(&array, NUMBER_VAL(1));
    writeValueArray(&array, NUMBER_VAL(2));
    
    freeValueArray(&array);
    
    EXPECT_EQ(array.count, 0);
    EXPECT_EQ(array.capacity, 0);
}
