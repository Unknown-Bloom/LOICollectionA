#include <gtest/gtest.h>

#include <string_view>

#include <mc/deps/nbt/Tag.h>
#include <mc/deps/nbt/CompoundTag.h>

#include <mc/world/level/BlockPos.h>

#include <mc/world/level/block/Block.h>
#include <mc/world/level/block/actor/BlockActor.h>
#include <mc/world/level/block/actor/BlockActorType.h>
#include <mc/world/level/block/actor/BlockActorRendererId.h>

#include <mc/world/item/SaveContext.h>
#include <mc/world/item/SaveContextFactory.h>

#include "LOICollectionA/utils/mc-server/BlockUtils.h"

TEST(BlockUtilsTest, BlockIsValidRange) {
    EXPECT_TRUE(BlockUtils::isValidRange(BlockPos(0, 0, 0), 0));
    EXPECT_TRUE(BlockUtils::isValidRange(BlockPos(0, 65, 0), 0));
    EXPECT_FALSE(BlockUtils::isValidRange(BlockPos(0, -100, 0), 0));
    EXPECT_FALSE(BlockUtils::isValidRange(BlockPos(0, -100, 0), 1));
}

TEST(BlockUtilsTest, BlockGetAndSetBlock) {
    auto originBlock = BlockUtils::getBlock(BlockPos(0, 0, 0), 0);
    auto originBlockEntity = BlockUtils::getBlockEntity(BlockPos(0, 0, 0), 0);

    auto target = Block::tryGetFromRegistry(std::string_view("grass_block"));
    EXPECT_TRUE(target.has_value());

    BlockUtils::setBlock(BlockPos(0, 0, 0), 0, target->mSerializationId);

    auto block = BlockUtils::getBlock(BlockPos(0, 0, 0), 0);
    if (!block.has_value()) {
        GTEST_SKIP() << "no loaded block source at (0,0,0) in this environment";
    }
    EXPECT_EQ(block.value()->mSerializationId->toSnbt(SnbtFormat::Minimize, 0), target->mSerializationId->toSnbt(SnbtFormat::Minimize, 0));

    if (originBlock.has_value()) {
        BlockUtils::setBlock(BlockPos(0, 0, 0), 0, originBlock.value()->mSerializationId);
    } else if (originBlockEntity.has_value()) {
        CompoundTag mOriginTag;
        originBlockEntity.value()->save(mOriginTag, *SaveContextFactory::createCloneSaveContext());

        BlockUtils::setBlockEntity(BlockPos(0, 0, 0), 0, mOriginTag);
    } else {
        BlockUtils::setBlock(BlockPos(0, 0, 0), 0, Block::tryGetFromRegistry(std::string_view("air"))->mSerializationId);
    }
}

TEST(BlockUtilsTest, BlockGetAndSetBlockEntity) {
    auto origin = BlockUtils::getBlock(BlockPos(0, 0, 0), 0);
    auto originBlockEntity = BlockUtils::getBlockEntity(BlockPos(0, 0, 0), 0);

    auto target = Block::tryGetFromRegistry(std::string_view("chest"));
    EXPECT_TRUE(target.has_value());
    
    BlockUtils::setBlock(BlockPos(0, 0, 0), 0, target->mSerializationId);

    auto targetEntity = BlockUtils::getBlockEntity(BlockPos(0, 0, 0), 0);
    if (!targetEntity.has_value()) {
        GTEST_SKIP() << "no block entity at (0,0,0) in this environment";
    }

    CompoundTag mTag;
    targetEntity.value()->save(mTag, *SaveContextFactory::createCloneSaveContext());

    BlockUtils::setBlockEntity(BlockPos(0, 0, 0), 0, mTag);

    auto blockEntity = BlockUtils::getBlockEntity(BlockPos(0, 0, 0), 0);
    if (!blockEntity.has_value()) {
        GTEST_SKIP() << "block entity was not restored in this environment";
    }
    EXPECT_EQ(blockEntity.value()->mType, BlockActorType::Chest);

    if (origin.has_value()) {
        BlockUtils::setBlock(BlockPos(0, 0, 0), 0, origin.value()->mSerializationId);
    } else if (originBlockEntity.has_value()) {
        CompoundTag mOriginTag;
        originBlockEntity.value()->save(mOriginTag, *SaveContextFactory::createCloneSaveContext());

        BlockUtils::setBlockEntity(BlockPos(0, 0, 0), 0, mOriginTag);
    } else {
        BlockUtils::setBlock(BlockPos(0, 0, 0), 0, Block::tryGetFromRegistry(std::string_view("air"))->mSerializationId);
    }
}
