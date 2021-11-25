#pragma once
#include <AzCore/Asset/AssetCommon.h>
#include <AzCore/std/string/string.h>
#include <AzCore/Memory/SystemAllocator.h>
#include <AzCore/IO/Path/Path.h>
#include <AzToolsFramework/Asset/AssetUtils.h>
#include <AzFramework/Platform/PlatformDefaults.h>
#include <AzCore/JSON/document.h>
#include <AzCore/JSON/rapidjson.h>

// Most of the code here are copied straight from AssetBundler. As much as possible we don't want to change anything in o3de code.
namespace BRAssetBundler
{
    typedef rapidjson::GenericObject<true, rapidjson::Value> JsonObject;

    class AssetPackInfo
    {
    public:
        AZ_TYPE_INFO(AssetPackInfo, "{0587D8BE-64EF-470E-AB6E-C34F481567CF}");

        AssetPackInfo();
        AssetPackInfo(const AZ::Data::AssetId& assetId, const AZStd::string& path, AZ::u32 packId);
        AssetPackInfo(const AZStd::string& path);

        AssetPackInfo& operator=(const AssetPackInfo& rhs);

        bool operator==(const AssetPackInfo& rhs) const;
        bool operator!=(const AssetPackInfo& rhs) const;

        void JsonLoad(AZ::u32 packId, const JsonObject& jsonObject, AzFramework::PlatformFlags platformFlags);
        AZStd::string JsonStore(rapidjson::Value& outValue, rapidjson::Document::AllocatorType& allocator);

        AZ::Data::AssetId m_assetId;
        AZStd::string m_assetRelativePath;
        AZ::u32 m_packId = 0;

        AZStd::string m_bundlePath;
        AZ::u32 m_offset = 0;
        AZ::u32 m_size = 0;
        AZ::u32 m_headerOffset = 0;
        AZ::u32 m_headerSize = 0;
    };

    //! list of asset ids : unordered
    using AssetIdList = AZStd::unordered_set<AZ::Data::AssetId>;
    //! list of asset pack info : unordered
    using AssetPackInfoList = AZStd::unordered_set<AssetPackInfo>;

    //! A map where the key is the asset Id, value is the pack id
    using AssetIdPackIdMap = AZStd::map<AZ::Data::AssetId, AZ::u32>;
    //! Map where the key is the pack id, value is the asset id list
    using IdAssetIdListMap = AZStd::map<AZ::u32, AssetIdList>;
    //! Map where the key is the pack id, value is the list of asset pack info
    using IdPackInfoListMap = AZStd::map<AZ::u32, AssetPackInfoList>;
    //!A map where the key is the asset Id, value is asset pack info
    using AssetPackInfoMap = AZStd::map<AZ::Data::AssetId, AssetPackInfo>;
    //! A map where the key is the asset path, value is asset pack info
    using PathPackInfoMap = AZStd::map<AZStd::string, AssetPackInfo>;

    enum CommandType
    {
        Invalid,
        Seeds,
        AssetLists,
        ComparisonRules,
        Compare,
        BundleSettings,
        Bundles,
        BundleSeed,
        MergeAssetHints
    };

    extern const AZ::u32 DefaultPackIdValue;

    ////////////////////////////////////////////////////////////////////////////////////////////
    // General
    extern const char* AppWindowName;
    extern const char* AppWindowNameVerbose;
    extern const char* HelpFlag;
    extern const char* HelpFlagAlias;
    extern const char* VerboseFlag;
    extern const char* PlatformArg;
    extern const char* PrintFlag;
    extern const char* AssetCatalogFileArg;
    extern const char* AllowOverwritesFlag;
    extern const char* IgnoreFileCaseFlag;
    extern const char* PackIdFirstMarker;
    extern const char* PackIdSecondMarker;
    extern const char* AssetHintsExtension;
    extern const char* BPakExtension;
    extern const char* PakExtension;
    extern const char* PakAssetHintsExtension;
    extern const char* SeedAssetHintsExtension;
    extern const char* SamplingLogExtension;
    extern const char* AssetBundlerBatchName;
    extern const char* RegsetFlag;
    extern const char* LevelsPathPattern;
    extern const char* ProjectArg;
    extern const char* PackIdArg;
    
    // Seeds
    extern const char* SeedsCommand;
    extern const char* SeedListFileArg;
    extern const char* AddSeedArg;
    extern const char* RemoveSeedArg;
    extern const char* AddPlatformToAllSeedsFlag;
    extern const char* RemovePlatformFromAllSeedsFlag;
    extern const char* UpdateSeedPathArg;
    extern const char* RemoveSeedPathArg;

    // Asset Lists
    extern const char* AssetListsCommand;
    extern const char* AssetListFileArg;
    extern const char* AddDefaultSeedListFilesFlag;
    extern const char* DryRunFlag;
    extern const char* GenerateDebugFileFlag;
    extern const char* SkipArg;

    ////////////////////////////////////////////////////////////////////////////////////////////
    // Comparison Rules
    extern const char* ComparisonRulesCommand;
    extern const char* ComparisonRulesFileArg;
    extern const char* ComparisonTypeArg;
    extern const char* ComparisonFilePatternArg;
    extern const char* ComparisonFilePatternTypeArg;
    extern const char* ComparisonTokenNameArg;
    extern const char* ComparisonFirstInputArg;
    extern const char* ComparisonSecondInputArg;
    extern const char* AddComparisonStepArg;
    extern const char* RemoveComparisonStepArg;
    extern const char* MoveComparisonStepArg;
    extern const char* EditComparisonStepArg;
    ////////////////////////////////////////////////////////////////////////////////////////////

    ////////////////////////////////////////////////////////////////////////////////////////////
    // Compare
    extern const char* CompareCommand;
    extern const char* CompareFirstFileArg;
    extern const char* CompareSecondFileArg;
    extern const char* CompareOutputFileArg;
    extern const char* ComparePrintArg;
    extern const char* IntersectionCountArg;
    ////////////////////////////////////////////////////////////////////////////////////////////

    ////////////////////////////////////////////////////////////////////////////////////////////
    // Bundle Settings
    extern const char* BundleSettingsCommand;
    extern const char* BundleSettingsFileArg;
    extern const char* OutputBundlePathArg;
    extern const char* BundleVersionArg;
    extern const char* MaxBundleSizeArg;

    // Bundles
    extern const char* BundlesCommand;

    // Bundle Seed
    extern const char* BundleSeedCommand;

    extern const char* MergeAssetHintsCommand;
    extern const char* AssetHintsFileArg;
    extern const char* OutputSamplingLogArg;

    extern const char* AssetCatalogFilename;
    ////////////////////////////////////////////////////////////////////////////////////////////

    //! Add the specified platform identifier to the filename
    void AddPlatformIdentifier(AZStd::string& filePath, const AZStd::string& platformIdentifier);

    //! Returns platformFlags of all enabled platforms by parsing all the asset processor config files.
    //! Please note that the game project could be in a different location to the engine therefore we need the assetRoot param.
    AzFramework::PlatformFlags GetEnabledPlatformFlags(
        AZStd::string_view enginePath, AZStd::string_view assetRoot, AZStd::string_view projectPath);

    //! Returns true if an existing key is found
    //! checks the infoMap provided if there's an existing key, if yes it will check if the new pack id is lesser than the current one and change it
    //! if no, adds new pair to map
    bool AddAssetPackInfoToMap(AssetPackInfoMap& infoMap, AZ::Data::AssetId assetId, const AZStd::string& assetRelativePath, AZ::u32 uPackId);
    bool AddAssetPackInfoToMap(AssetPackInfoMap& infoMap, const AssetPackInfo& packInfo);
    bool AddAssetPackInfoToMap(PathPackInfoMap& infoMap, const AssetPackInfo& packInfo);
    
    void RemoveAssetPackInfoFromMap(AssetPackInfoMap& infoMap, const AZ::Data::AssetId& assetId);
    void WriteAssetHints(IdPackInfoListMap infoMap, const AZStd::string& filePath);
    AZ::Outcome<void, AZStd::string> ReadAssetHints(const AZStd::string& filePath, AzFramework::PlatformFlags platformFlags, AZStd::function<void(AssetPackInfo)> callback);
    AZ::Outcome<void, AZStd::string> WriteSamplingLogs(AZStd::string_view filePath, IdPackInfoListMap infoMap);

    //! direct copy of AssetSeedManager's GetSeedPath
    AZStd::string GetAssetPathById(AZ::Data::AssetId assetId, AzFramework::PlatformFlags platformFlags);
    //! direct copy of AssetSeedManager's GetAssetIdByPath
    AZ::Data::AssetId GetAssetIdByPath(const AZStd::string& assetPath, const AzFramework::PlatformFlags& platformFlags);

    template<typename T>
    void ConvertMapToPackIdKeyedMap(T assetIdMap, IdPackInfoListMap& packIdMap);

    //! Filepath is a helper class that is used to find the absolute path of a file
    //! if the inputted file path is an absolute path than it does nothing
    //! if the inputted file path is a relative path than based on whether the user
    //! also inputted a root directory it computes the absolute path,
    //! if root directory is provided it uses that otherwise it uses the engine root as the default root folder.
    class FilePath
    {
    public:
        AZ_CLASS_ALLOCATOR(FilePath, AZ::SystemAllocator, 0);
        explicit FilePath(
            const AZStd::string& filePath,
            AZStd::string platformIdentifier = AZStd::string(),
            bool checkFileCase = false,
            bool ignoreFileCase = false);
        explicit FilePath(const AZStd::string& filePath, bool checkFileCase, bool ignoreFileCase);
        FilePath() = default;
        const AZStd::string& AbsolutePath() const;
        const AZStd::string& OriginalPath() const;
        AZStd::string ErrorString() const;
        bool IsValid() const;

    private:
        void ComputeAbsolutePath(const AZStd::string& platformIdentifier, bool checkFileCase, bool ignoreFileCase);

        AZ::IO::Path m_absolutePath;
        AZ::IO::Path m_originalPath;
        AZStd::string m_errorString;
        bool m_validPath = false;
    };

    bool LooksLikeWildcardPattern(const AZStd::string& inputPattern);

    // Forward declare platform specific functions
    namespace Platform
    {
        void ParseConsoleOutputFromListFilesInArchive(const AZStd::string& consoleOutput, const AZStd::string& bundlePath, PathPackInfoMap& outInfoMap);
    } // namespace Platform
}

namespace AZStd
{
    /**
     * AssetPackInfo Asset IDs to be keys in hashed data structures.
     */
    template<>
    struct hash<BRAssetBundler::AssetPackInfo>
    {
        typedef BRAssetBundler::AssetPackInfo argument_type;
        typedef AZStd::size_t result_type;
        size_t operator()(const BRAssetBundler::AssetPackInfo& info) const
        {
            return info.m_assetId.m_guid.GetHash();
        }
    };
}
