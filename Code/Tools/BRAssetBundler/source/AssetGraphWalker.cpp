#include <source/AssetGraphWalker.h>
#include <AzToolsFramework/AssetCatalog/PlatformAddressedAssetCatalogBus.h>
namespace BRAssetBundler
{
    void AssetGraphWalker::CascadeValuesToMap(
        AssetPackInfoMap& outMap,
        IdAssetIdListMap sourcePackIdMap,
        const AzFramework::PlatformId& platformIndex,
        const AZStd::unordered_set<AZ::Data::AssetId>& exclusionList,
        const AZStd::vector<AZStd::string>& wildcardPatternExclusionList)
    {
        AZStd::unordered_set<AZ::Data::AssetId> cyclicalDependencySet;
        for (auto iter = sourcePackIdMap.rbegin(); iter != sourcePackIdMap.rend(); ++iter) //iterate from the end of the map
        {
            for (const auto& assetId : iter->second)
            {
                cyclicalDependencySet.clear();
                if (outMap.count(assetId) > 0)
                {
                    outMap[assetId].m_packId = iter->first;
                    SetValueToDescendants(assetId, outMap, iter->first, platformIndex, &cyclicalDependencySet, exclusionList, wildcardPatternExclusionList);
                }
            }
        }
    }

    void AssetGraphWalker::SetValueToDescendants(
        AZ::Data::AssetId assetId,
        AssetPackInfoMap& outMap,
        AZ::u32 uPackId,
        const AzFramework::PlatformId& platformIndex,
        AZStd::unordered_set<AZ::Data::AssetId>* cyclicalDependencySet,
        const AZStd::unordered_set<AZ::Data::AssetId>& exclusionList,
        const AZStd::vector<AZStd::string>& wildcardPatternExclusionList)
    {
        using namespace AzToolsFramework::AssetCatalog;

        if (!cyclicalDependencySet)
        {
            // A failure means there were no dependencies, and not that the call failed.
            return;
        }

        AZ::Outcome<AZStd::vector<AZ::Data::ProductDependency>, AZStd::string> currentDependenciesResult = AZ::Failure(AZStd::string());
        PlatformAddressedAssetCatalogRequestBus::EventResult(
            currentDependenciesResult, platformIndex, &PlatformAddressedAssetCatalogRequestBus::Events::GetDirectProductDependencies,
            assetId);
        if (!currentDependenciesResult.IsSuccess())
        {
            // A failure means there were no dependencies, and not that the call failed.
            return;
        }

        AZStd::vector<AZ::Data::ProductDependency> entries = currentDependenciesResult.TakeValue();

        cyclicalDependencySet->insert(assetId);

        for (const AZ::Data::ProductDependency& productDependency : entries)
        {
            if (!productDependency.m_assetId.IsValid())
            {
                continue;
            }

            if (exclusionList.find(productDependency.m_assetId) != exclusionList.end())
            {
                continue;
            }

            bool wildcardPatternMatch = false;
            for (const AZStd::string& wildcardPattern : wildcardPatternExclusionList)
            {
                PlatformAddressedAssetCatalogRequestBus::EventResult(wildcardPatternMatch, platformIndex, &PlatformAddressedAssetCatalogRequestBus::Events::DoesAssetIdMatchWildcardPattern, assetId, wildcardPattern);
                if (wildcardPatternMatch)
                {
                    break;
                }
            }
            if (wildcardPatternMatch)
            {
                continue;
            }

            if (outMap.count(productDependency.m_assetId) > 0)
            {
                outMap[productDependency.m_assetId].m_packId = uPackId;
            }
                

            // Cyclical Dependency detection
            if (cyclicalDependencySet->find(productDependency.m_assetId) != cyclicalDependencySet->end())
            {
                continue;
            }

            // Recurse
            SetValueToDescendants(productDependency.m_assetId, outMap, uPackId, platformIndex, cyclicalDependencySet, exclusionList, wildcardPatternExclusionList);
        }

        cyclicalDependencySet->erase(assetId);
    }
}

