#include <memory>

#include <ll/api/service/Bedrock.h>

#include <mc/deps/nbt/CompoundTag.h>

#include <mc/world/level/Level.h>
#include <mc/world/level/BlockPos.h>
#include <mc/world/level/BlockSource.h>
#include <mc/world/level/ChunkBlockPos.h>
#include <mc/world/level/ChunkLocalHeight.h>

#include <mc/world/level/block/Block.h>
#include <mc/world/level/block/BlockChangeContext.h>
#include <mc/world/level/block/actor/BlockActor.h>

#include <mc/world/level/chunk/LevelChunk.h>
#include <mc/world/level/dimension/Dimension.h>

#include <mc/dataloadhelper/DefaultDataLoadHelper.h>

#include "LOICollectionA/utils/mc-server/BlockUtils.h"

DefaultDataLoadHelper mDataLoadHelper;

namespace BlockUtils {
    bool isValidRange(const BlockPos& pos, int dimension) {
        auto mDimension = ll::service::getLevel()->getDimension(dimension).lock();
        if (!mDimension || pos.y > mDimension->mHeightRange->mMax || pos.y < mDimension->mHeightRange->mMin)
            return false;

        return true;
    }

    std::optional<const Block*> getBlock(const BlockPos& pos, int dimension) {
        if (!isValidRange(pos, dimension))
            return std::nullopt;

        auto mDimension = ll::service::getLevel()->getDimension(dimension).lock();
        BlockSource& mBlockSource = mDimension->getBlockSourceFromMainChunkSource();
        LevelChunk* mLevelChunk = mBlockSource.getChunkAt(pos);
        if (!mLevelChunk)
            return std::nullopt;

        ChunkLocalHeight mLocalHeight{ static_cast<short>(pos.y - mDimension->mHeightRange->mMin) };
        ChunkBlockPos mChunkBlockPos(static_cast<uchar>(pos.x & 0xf), mLocalHeight, static_cast<uchar>(pos.z & 0xf));
                
        auto mBlock = &mLevelChunk->getBlock(mChunkBlockPos);
        return mBlock ? std::optional<const Block*>{ mBlock } : std::nullopt;
    }

    std::optional<BlockActor*> getBlockEntity(const BlockPos& pos, int dimension) {
        if (!isValidRange(pos, dimension))
            return std::nullopt;

        auto mDimension = ll::service::getLevel()->getDimension(dimension).lock();
        BlockSource& mBlockSource = mDimension->getBlockSourceFromMainChunkSource();
        BlockActor* mBlockEntity = mBlockSource.getBlockEntity(pos);
        return mBlockEntity ? std::optional(mBlockEntity) : std::nullopt;
    }

    void setBlock(const BlockPos& pos, int dimension, const CompoundTag& nbt) {
        if (!isValidRange(pos, dimension))
            return;

        auto mDimension = ll::service::getLevel()->getDimension(dimension).lock();
        BlockSource& mBlockSource = mDimension->getBlockSourceFromMainChunkSource();
        if (auto mBlock = Block::tryGetFromRegistry(nbt); mBlock.has_value()) 
            mBlockSource.setBlock(pos, mBlock.value(), 3, nullptr, BlockChangeContext(StatelessBlockChangeContext::Commands));
    }

    void setBlockEntity(const BlockPos& pos, int dimension, const CompoundTag& nbt) {
        if (!isValidRange(pos, dimension))
            return;
        
        auto mDimension = ll::service::getLevel()->getDimension(dimension).lock();
        BlockSource& mBlockSource = mDimension->getBlockSourceFromMainChunkSource();
        if (auto mBlockActor = mBlockSource.getBlockEntity(pos); mBlockActor)
            mBlockActor->load(*ll::service::getLevel(), nbt, mDataLoadHelper);
    }
}