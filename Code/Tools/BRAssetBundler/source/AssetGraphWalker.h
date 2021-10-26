#pragma once

#include "source/utils.h"

namespace BRAssetBundler
{
    class AssetGraphWalker
    {
    public:
        AZ_TYPE_INFO(AssetGraphWalker, "{FDE6AED5-71F5-46AC-BE27-5C0DE182423D}");
        AZ_CLASS_ALLOCATOR(AssetGraphWalker, AZ::SystemAllocator, 0);

        AssetGraphWalker() = default;

        void CascadeValuesToMap(AssetPackInfoMap& outMap, IdAssetIdListMap sourcePackIdMap, const AzFramework::PlatformId& platformIndex, const AZStd::unordered_set<AZ::Data::AssetId>& exclusionList, const AZStd::vector<AZStd::string>& wildcardPatternExclusionList);

        void SetValueToDescendants(AZ::Data::AssetId assetId, AssetPackInfoMap& outMap, AZ::u32 uPackId, const AzFramework::PlatformId& platformIndex, AZStd::unordered_set<AZ::Data::AssetId>* cyclicalDependencySet, const AZStd::unordered_set<AZ::Data::AssetId>& exclusionList, const AZStd::vector<AZStd::string>& wildcardPatternExclusionList);

    };
}
