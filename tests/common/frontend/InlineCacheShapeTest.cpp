#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "LOICollectionA/frontend/AST.h"
#include "common/frontend/CommonTest.h"

using namespace LOICollection::frontend;

TEST(InlineCacheShapeTest, ExtendShapeMemoizesTransitions) {
    auto base = std::make_shared<const FieldLayout>(std::vector<std::string>{"a", "b"});

    const FieldLayout* e1 = extendShape(base.get(), "c").get();
    const FieldLayout* e2 = extendShape(base.get(), "c").get();
    EXPECT_EQ(e1, e2);
    EXPECT_EQ(e1->names.size(), 3u);
    EXPECT_EQ(e1->slotOf("c"), 2);

    const FieldLayout* e3 = extendShape(base.get(), "d").get();
    EXPECT_NE(e1, e3);
    EXPECT_EQ(e3->slotOf("d"), 2);

    const FieldLayout* e4 = extendShape(e1, "d").get();
    EXPECT_NE(e4, e3);
    EXPECT_EQ(e4->names.size(), 4u);
    EXPECT_EQ(e4->slotOf("d"), 3);

    EXPECT_EQ(extendShape(base.get(), "c").get(), e1);
}

TEST(InlineCacheShapeTest, DynamicFieldMigratesToSlot) {
    auto obj = std::make_shared<Object>();
    obj->className = "Point";

    obj->assign("x", ValueNode::ValueType(1));
    obj->assign("y", ValueNode::ValueType(2));

    ASSERT_NE(obj->layout, nullptr);
    EXPECT_EQ(obj->slotOf("x"), 0);
    EXPECT_EQ(obj->slotOf("y"), 1);

    auto* fx = obj->find("x");
    ASSERT_NE(fx, nullptr);
    EXPECT_EQ(std::get<int>(*fx), 1);

    auto* fy = obj->find("y");
    ASSERT_NE(fy, nullptr);
    EXPECT_EQ(std::get<int>(*fy), 2);
}

TEST(InlineCacheShapeTest, ObjectsWithSameDynamicShapeShareLayout) {
    auto base = std::make_shared<const FieldLayout>(std::vector<std::string>{"x"});

    auto a = std::make_shared<Object>();
    a->layout = base;
    a->resize(1);
    a->addField("y", ValueNode::ValueType(2));

    auto b = std::make_shared<Object>();
    b->layout = base;
    b->resize(1);
    b->addField("y", ValueNode::ValueType(20));

    EXPECT_EQ(a->layout.get(), b->layout.get());
    EXPECT_EQ(std::get<int>(*b->find("y")), 20);
}

TEST(InlineCacheShapeTest, ReassignExistingFieldKeepsShape) {
    auto obj = std::make_shared<Object>();
    obj->assign("k", ValueNode::ValueType(1));

    const FieldLayout* before = obj->layout.get();
    obj->assign("k", ValueNode::ValueType(2));

    EXPECT_EQ(obj->layout.get(), before);
    EXPECT_EQ(std::get<int>(*obj->find("k")), 2);
}

TEST(InlineCacheShapeTest, PolymorphicSiteResolvesHeterogeneousShapes) {
    EXPECT_EQ(eval(
        "class Base { public: x = 0; } "
        "class A extends Base { public: x = 1; } "
        "class B extends Base { public: x = 2; } "
        "func getX(o: Base) -> int { return o.x; } "
        "let a = new A(); let b = new B(); "
        "getX(a) + getX(b)"), "3");
}

TEST(InlineCacheShapeTest, PolymorphicCacheReusedAcrossCalls) {
    EXPECT_EQ(eval(
        "class Base { public: x = 0; } "
        "class A extends Base { public: x = 1; } "
        "class B extends Base { public: x = 2; } "
        "func getX(o: Base) -> int { return o.x; } "
        "let a = new A(); let b = new B(); "
        "getX(a) + getX(b) + getX(a) + getX(b)"), "6");
}

TEST(InlineCacheShapeTest, MissingFieldStillErrors) {
    EXPECT_THROW(eval(
        "class A { public: x = 1; } "
        "let a = new A(); "
        "a.y"), std::runtime_error);
}
