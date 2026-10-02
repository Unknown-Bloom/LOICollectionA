#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>

#include "LOICollectionA/base/Ownership.h"

namespace ownership_test_support {
    struct Tracked {
        static inline int alive = 0;

        std::string name;
        int value = 0;

        Tracked() { ++alive; }
        explicit Tracked(std::string name, int value) : name(std::move(name)), value(value) { ++alive; }
        Tracked(const Tracked& other) : name(other.name), value(other.value) { ++alive; }
        Tracked(Tracked&& other) noexcept : name(std::move(other.name)), value(other.value) { ++alive; }

        ~Tracked() { --alive; }
    };
}

using ownership_test_support::Tracked;

TEST(OwnershipTest, ObserverIsNonOwningPointer) {
    static_assert(std::is_same_v<observer<int>, int*>);
    static_assert(std::is_same_v<observer<Tracked>, Tracked*>);

    Tracked value("owned", 7);
    observer<Tracked> view = &value;

    EXPECT_EQ(view->name, "owned");
    EXPECT_EQ(view->value, 7);
}

TEST(OwnershipTest, MakeOwnedForwardsConstructorArguments) {
    const int before = Tracked::alive;

    {
        std::unique_ptr<Tracked> owned = makeOwned<Tracked>("std::string", 42);

        ASSERT_NE(owned, nullptr);
        EXPECT_EQ(owned->name, "std::string");
        EXPECT_EQ(owned->value, 42);
        EXPECT_EQ(Tracked::alive, before + 1);
    }

    EXPECT_EQ(Tracked::alive, before);
}

TEST(OwnershipTest, MakeOwnedDefaultConstructs) {
    std::unique_ptr<Tracked> owned = makeOwned<Tracked>();

    ASSERT_NE(owned, nullptr);
    EXPECT_EQ(owned->value, 0);
}

TEST(OwnershipTest, MakeSharedCountsReferences) {
    const int before = Tracked::alive;

    {
        std::shared_ptr<Tracked> shared = makeShared<Tracked>("shared", 3);
        ASSERT_NE(shared, nullptr);
        EXPECT_EQ(shared.use_count(), 1L);

        std::shared_ptr<Tracked> copy = shared;
        EXPECT_EQ(shared.use_count(), 2L);
        EXPECT_EQ(copy->value, 3);
        EXPECT_EQ(Tracked::alive, before + 1);
    }

    EXPECT_EQ(Tracked::alive, before);
}

TEST(OwnershipTest, ObserverStaysValidWhileAnySharedOwnerLives) {
    std::shared_ptr<Tracked> owner = makeShared<Tracked>("watched", 9);
    observer<Tracked> view = owner.get();

    {
        std::shared_ptr<Tracked> copy = owner;
        EXPECT_EQ(copy.use_count(), 2L);
        EXPECT_EQ(view->value, 9);
    }

    EXPECT_EQ(view->name, "watched");
    EXPECT_EQ(owner.use_count(), 1L);
}

TEST(OwnershipTest, MakeSharedDoesNotCopyWhenMovingFromOwner) {
    const int before = Tracked::alive;

    std::shared_ptr<Tracked> owner = makeShared<Tracked>("moved", 5);
    std::shared_ptr<Tracked> moved = std::move(owner);

    EXPECT_EQ(owner, nullptr);
    EXPECT_EQ(moved.use_count(), 1L);
    EXPECT_EQ(Tracked::alive, before + 1);
}
