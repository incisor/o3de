#include <source/utils.h>
#include <AzToolsFramework/Asset/AssetBundler.h>
#include <AzFramework/StringFunc/StringFunc.h>
#include <AzCore/Utils/Utils.h>
#include <AzCore/Serialization/Json/JsonUtils.h>
#include <AzCore/JSON/error/en.h>
#include <AzToolsFramework/AssetCatalog/PlatformAddressedAssetCatalogBus.h>

namespace BRAssetBundler
{
    // General
    const char* AppWindowName = "BRAssetBundler";
    const char* AppWindowNameVerbose = "BRAssetBundlerVerbose";
    const char* HelpFlag = "help";
    const char* HelpFlagAlias = "h";
    const char* VerboseFlag = "verbose";
    const char* PlatformArg = "platform";
    const char* PrintFlag = "print";
    const char* AssetCatalogFileArg = "overrideAssetCatalogFile";
    const char* AllowOverwritesFlag = "allowOverwrites";
    const char* IgnoreFileCaseFlag = "ignoreFileCase";
    const char* PackIdFirstMarker = "[";
    const char* PackIdSecondMarker = "]";
    const char* AssetHintsExtension = ".assethints";
    const char* BPakExtension = ".bpak";
    const char* PakExtension = ".pak";
    const char* PakAssetHintsExtension = "pak.assethints";
    const char* SeedAssetHintsExtension = "seed.assethints";
    const char* ProfilingLogExtension = ".proflog";
    const char* AssetBundlerBatchName = "AssetBundlerBatch.exe";
    const char* RegsetFlag = "regset";
    const char* LevelsPathPattern = "*levels\\*\\*.*";
    const char* ProjectArg = "project-path";
    
    // Seeds
    const char* SeedsCommand = "seeds";
    const char* SeedListFileArg = "seedListFile";
    const char* AddSeedArg = "addSeed";
    const char* RemoveSeedArg = "removeSeed";
    
    // Asset Lists
    const char* AssetListsCommand = "assetLists";
    const char* AssetListFileArg = "assetListFile";
    const char* SkipArg = "skip";

    // Comparison Rules
    const char* ComparisonRulesCommand = "comparisonRules";

    // Compare
    const char* CompareCommand = "compare";

    // Bundle Settings 
    const char* BundleSettingsCommand = "bundleSettings";
    const char* BundleSettingsFileArg = "bundleSettingsFile";
    const char* OutputBundlePathArg = "outputBundlePath";
    const char* BundleVersionArg = "bundleVersion";
    const char* MaxBundleSizeArg = "maxSize";

    // Bundles
    const char* BundlesCommand = "bundles";

    // Bundle Seed
    const char* BundleSeedCommand = "bundleSeed";

    const char* AssetCatalogFilename = "assetcatalog.xml";

    //JSON Key Names
    const char* GuidKey = "guid";
    const char* SubIdKey = "subId";
    const char* AssetHintKey = "assetHint";

    ////////////////////////////////////////////////////////////////////////////////////////////
    // AssetPackInfo
    ////////////////////////////////////////////////////////////////////////////////////////////

    void AddPlatformIdentifier(AZStd::string& filePath, const AZStd::string& platformIdentifier)
    {
        AZStd::string fileName;
        AzFramework::StringFunc::Path::GetFileName(filePath.c_str(), fileName);

        AZStd::string extension;
        AzFramework::StringFunc::Path::GetExtension(filePath.c_str(), extension);
        AZStd::string platformSuffix = AZStd::string::format("_%s", platformIdentifier.c_str());

        fileName = AZStd::string::format("%s%s", fileName.c_str(), platformSuffix.c_str());
        AzFramework::StringFunc::Path::ReplaceFullName(filePath, fileName.c_str(), extension.c_str());
    }

    FilePath::FilePath(const AZStd::string& filePath, AZStd::string platformIdentifier, bool checkFileCase, bool ignoreFileCase)
    {
        AZStd::string platform = platformIdentifier;
        if (!platform.empty())
        {
            AZStd::string filePlatform = AzToolsFramework::GetPlatformIdentifier(filePath);
            if (!filePlatform.empty())
            {
                // input file path already has a platform, no need to append a platform id
                platform = AZStd::string();

                if (!AzFramework::StringFunc::Equal(filePlatform.c_str(), platformIdentifier.c_str(), true))
                {
                    // Platform identifier does not match the current platform
                    return;
                }
            }
        }

        if (!filePath.empty())
        {
            m_validPath = true;
            m_absolutePath = AZ::IO::PathView(filePath).LexicallyNormal();
            m_originalPath = m_absolutePath;
            ComputeAbsolutePath(platform, checkFileCase, ignoreFileCase);
        }
    }

    AzFramework::PlatformFlags GetEnabledPlatformFlags(
        AZStd::string_view engineRoot, AZStd::string_view assetRoot, AZStd::string_view projectPath)
    {
        auto settingsRegistry = AZ::SettingsRegistry::Get();
        if (settingsRegistry == nullptr)
        {
            AZ_Error(BRAssetBundler::AppWindowName, false, "Settings Registry is not available, enabled platform flags cannot be queried");
            return AzFramework::PlatformFlags::Platform_NONE;
        }

        auto configFiles = AzToolsFramework::AssetUtils::GetConfigFiles(engineRoot, assetRoot, projectPath, true, true, settingsRegistry);
        auto enabledPlatformList = AzToolsFramework::AssetUtils::GetEnabledPlatforms(*settingsRegistry, configFiles);
        AzFramework::PlatformFlags platformFlags = AzFramework::PlatformFlags::Platform_NONE;
        for (const auto& enabledPlatform : enabledPlatformList)
        {
            AzFramework::PlatformFlags platformFlag = AzFramework::PlatformHelper::GetPlatformFlag(enabledPlatform);

            if (platformFlag != AzFramework::PlatformFlags::Platform_NONE)
            {
                platformFlags = platformFlags | platformFlag;
            }
            else
            {
                AZ_Warning(
                    BRAssetBundler::AppWindowName, false, "Platform Helper is not aware of the platform (%s).\n ", enabledPlatform.c_str());
            }
        }

        return platformFlags;
    }


    bool AddAssetPackInfoToMap(AssetPackInfoMap& infoMap, AZ::Data::AssetId assetId, const AZStd::string& assetRelativePath, AZ::u32 uPackId)
    {
        if (infoMap.count(assetId) > 0) // an existing key is found
        {
            if (uPackId < infoMap[assetId].m_packId) // if new pack id is lesser than the current one we replace it
                infoMap[assetId].m_packId = uPackId;

            return true;
        }
        infoMap[assetId].m_assetId = assetId;
        infoMap[assetId].m_packId = uPackId;
        infoMap[assetId].m_assetRelativePath = assetRelativePath;

        return false;
    }

    bool AddAssetPackInfoToMap(AssetPackInfoMap& infoMap, const AssetPackInfo& packInfo)
    {
        if (infoMap.count(packInfo.m_assetId) > 0) // an existing key is found
        {
            if (packInfo.m_packId < infoMap[packInfo.m_assetId].m_packId) // if new pack id is lesser than the current one we replace it
                infoMap[packInfo.m_assetId].m_packId = packInfo.m_packId;

            return true;
        }

        infoMap[packInfo.m_assetId] = packInfo;
        return false;
    }

    bool AddAssetPackInfoToMap(PathPackInfoMap& infoMap, const AssetPackInfo& packInfo)
    {
        if (infoMap.count(packInfo.m_assetRelativePath) > 0) // an existing key is found
        {
            return true; // skip the add if found
        }

        infoMap[packInfo.m_assetRelativePath] = packInfo;
        return false;
    }

    void RemoveAssetPackInfoFromMap(AssetPackInfoMap& infoMap, const AZ::Data::AssetId& assetId)
    {
        if (infoMap.count(assetId) > 0) // an existing key is found
        {
            infoMap.erase(assetId);
        }
    }

    FilePath::FilePath(const AZStd::string& filePath, bool checkFileCase, bool ignoreFileCase)
        : FilePath(filePath, AZStd::string(), checkFileCase, ignoreFileCase)
    {
    }

    const AZStd::string& FilePath::AbsolutePath() const
    {
        return m_absolutePath.Native();
    }

    const AZStd::string& FilePath::OriginalPath() const
    {
        return m_originalPath.Native();
    }

    bool FilePath::IsValid() const
    {
        return m_validPath;
    }

    AZStd::string FilePath::ErrorString() const
    {
        return m_errorString;
    }

    void FilePath::ComputeAbsolutePath(const AZStd::string& platformIdentifier, bool checkFileCase, bool ignoreFileCase)
    {
        if (AzToolsFramework::AssetFileInfoListComparison::IsTokenFile(m_absolutePath.Native()))
        {
            return;
        }

        if (!platformIdentifier.empty())
        {
            BRAssetBundler::AddPlatformIdentifier(m_absolutePath.Native(), platformIdentifier);
        }

        AZ::IO::Path enginePath = AZ::IO::PathView(AZ::Utils::GetEnginePath());
        m_absolutePath = enginePath / m_absolutePath;
        if (checkFileCase)
        {
            AZ::IO::Path relFilePath = m_absolutePath.LexicallyProximate(enginePath);
            if (AzToolsFramework::AssetUtils::UpdateFilePathToCorrectCase(enginePath.Native(), relFilePath.Native()))
            {
                if (ignoreFileCase)
                {
                    m_absolutePath = (enginePath / relFilePath).String();
                }
                else
                {
                    AZ::IO::Path absfilePath = (enginePath / relFilePath).LexicallyNormal();
                    if (absfilePath != AZ::IO::PathView(m_absolutePath))
                    {
                        m_errorString = AZStd::string::format(
                            "File case mismatch, file ( %s ) does not exist on disk, did you mean file ( %s )."
                            " Please run the command again with the correct file path or use ( --%s ) arg if you want to allow case "
                            "insensitive file match.\n",
                            m_absolutePath.c_str(), absfilePath.c_str(), IgnoreFileCaseFlag);
                        m_validPath = false;
                    }
                }
            }
        }
    }

    bool LooksLikeWildcardPattern(const AZStd::string& inputPattern)
    {
        for (auto thisChar : inputPattern)
        {
            if (thisChar == '*' || thisChar == '?')
            {
                return true;
            }
        }
        return false;
    }

    AZStd::string GetAssetPathById(AZ::Data::AssetId assetId, AzFramework::PlatformFlags platformFlags)
    {
        using namespace AzToolsFramework;
        auto platformIndices = AzFramework::PlatformHelper::GetPlatformIndicesInterpreted(platformFlags);
        for (const auto& platformId : platformIndices)
        {
            AZStd::string assetPath;
            AssetCatalog::PlatformAddressedAssetCatalogRequestBus::EventResult(
                assetPath, platformId, &AssetCatalog::PlatformAddressedAssetCatalogRequestBus::Events::GetAssetPathById, assetId);
            if (!assetPath.empty())
            {
                return assetPath;
            }
        }

        AZ_Warning(
            AppWindowName, false, "Unable to resolve path of Seed asset (%s) for the given platforms (%s).\n",
            assetId.ToString<AZStd::string>().c_str(), AzFramework::PlatformHelper::GetCommaSeparatedPlatformList(platformFlags).c_str());

        return {};
    }

    AZ::Data::AssetId GetAssetIdByPath(const AZStd::string& assetPath, const AzFramework::PlatformFlags& platformFlags)
    {
        using namespace AzToolsFramework::AssetCatalog;
        AZ::Data::AssetId assetId;
        auto platformIndices = AzFramework::PlatformHelper::GetPlatformIndicesInterpreted(platformFlags);
        bool foundInvalid = false;

        for (const auto& platformNum : platformIndices)
        {
            AZ::Data::AssetId foundAssetId;
            PlatformAddressedAssetCatalogRequestBus::EventResult(
                foundAssetId, platformNum, &PlatformAddressedAssetCatalogRequestBus::Events::GetAssetIdByPath, assetPath.c_str(),
                AZ::Data::s_invalidAssetType, false);
            if (!foundAssetId.IsValid())
            {
                AZ_Warning(
                    "AssetSeedManager", false, "Asset catalog does not know about the asset ( %s ) on platform ( %s ).", assetPath.c_str(),
                    AzFramework::PlatformHelper::GetPlatformName(platformNum));
                foundInvalid = true;
            }
            else
            {
                assetId = foundAssetId;
            }
        }
        if (foundInvalid)
        {
            return AZ::Data::AssetId();
        }
        return assetId;
    }

    AZ::Outcome<void, AZStd::string> ReadAssetHints(const AZStd::string& filePath, AzFramework::PlatformFlags platformFlags, AZStd::function<void(const AssetPackInfo&)> callback)
    {
        auto readResult = AZ::Utils::ReadFile<AZStd::string>(AZStd::string_view{ filePath });
        if (readResult.IsSuccess())
        {
            auto rawData = readResult.TakeValue();
            rapidjson::Document jsonDocument;
            jsonDocument.Parse<rapidjson::kParseCommentsFlag>(rawData.data(), rawData.size());
            if (jsonDocument.HasParseError())
            {
                size_t lineNumber = 1;

                const size_t errorOffset = jsonDocument.GetErrorOffset();
                for (size_t searchOffset = rawData.find('\n'); searchOffset < errorOffset && searchOffset < AZStd::string::npos;
                     searchOffset = rawData.find('\n', searchOffset + 1))
                {
                    lineNumber++;
                }

                return AZ::Failure(AZStd::string::format("JSON parse error at line %zu: %s", lineNumber, rapidjson::GetParseError_En(jsonDocument.GetParseError())));
            }
            else
            {
                for (rapidjson::Value::ConstMemberIterator itr = jsonDocument.MemberBegin(); itr != jsonDocument.MemberEnd(); ++itr)
                {
                    AZ::u32 uPackId = AZStd::stoi(AZStd::string(itr->name.GetString()));
                    if (itr->value.IsArray())
                    {
                        for (rapidjson::Value::ConstValueIterator arrayItr = itr->value.Begin(); arrayItr != itr->value.End(); ++arrayItr)
                        {
                            AssetPackInfo packInfo;
                            packInfo.JsonLoad(uPackId, arrayItr->GetObject(), platformFlags);
                            callback(packInfo);
                        }
                    }
                    else
                    {
                        return AZ::Failure(AZStd::string::format("Expecting an array but found another type. Check the file if it follows an assethint format.\n"));
                    }
                }
            }
        }
        else
        {
            return AZ::Failure(AZStd::string::format("%s\n", readResult.GetError().c_str()));
        }

        return AZ::Success();
    }

    void WriteAssetHints(IdPackInfoListMap infoMap, const AZStd::string& filePath)
    {
        if (!infoMap.empty())
        {
            rapidjson::Document jsonDoc;
            jsonDoc.SetObject();

            for (auto packInfo : infoMap)
            {
                rapidjson::Value assetPackInfoList(rapidjson::kArrayType);

                for (auto assetPackInfo : packInfo.second)
                {
                    rapidjson::Value assetPackInfoObject;
                    assetPackInfo.JsonStore(assetPackInfoObject, jsonDoc.GetAllocator());
                    assetPackInfoList.PushBack(assetPackInfoObject, jsonDoc.GetAllocator());
                }

                if (!packInfo.second.empty())
                {
                    AZStd::string packIdStr = AZStd::to_string(packInfo.first);
                    jsonDoc.AddMember(rapidjson::Value().SetString(packIdStr.c_str(), jsonDoc.GetAllocator()), assetPackInfoList, jsonDoc.GetAllocator());
                }
            }

            auto outcome = AZ::JsonSerializationUtils::WriteJsonFile(jsonDoc, filePath);
            if (!outcome.IsSuccess())
            {
                AZ_Error(
                        BRAssetBundler::AppWindowName, false,
                        "Failed to save '%s'."
                        "Error: %s",
                        filePath.c_str(), outcome.GetError().c_str());
            }
        }
    }

    AZ::Outcome<void, AZStd::string> WriteProfilingLogs(AZStd::string_view filePath, IdPackInfoListMap infoMap, PathPackInfoMap archiveInfoMap)
    {
        // open/create file for writing
        if (infoMap.empty() || archiveInfoMap.empty())
            return AZ::Failure(AZStd::string("empty map or list\n"));

        AZ::IO::FixedMaxPath filePathFixed = filePath; // Because FileIOStream requires a null-terminated string
        AZ::IO::FileIOStream stream(filePathFixed.c_str(), AZ::IO::OpenMode::ModeWrite);
        if (!stream.IsOpen())
            return AZ::Failure(AZStd::string::format("Could not write to file '%s'", filePathFixed.c_str()));

        AZStd::string lineStr;
        AZ::IO::SizeType bytesWritten;
        AZ::u32 uCtr = 0;
        for (auto list : infoMap)
        {
            uCtr++;
            for (auto info : list.second)
            {
                auto path = info.m_assetRelativePath;

                if (archiveInfoMap.count(path) <= 0)
                {
                    AZ_Printf(AppWindowName, "can't find %s\n", info.m_assetRelativePath.c_str());
                    continue;
                }

                // we only write the renamed file (i.e. if the bundle is named game.pak, it will be written as game.bpak)
                lineStr = AZStd::string::format(
                    "%s\t%u\t%u\ti-read \t000000000000000000\n", archiveInfoMap[path].m_bundlePath.c_str(),
                    archiveInfoMap[path].m_offset, archiveInfoMap[path].m_size);

                AZ_Printf(
                    AppWindowName, "%s %u %u\n", archiveInfoMap[path].m_bundlePath.c_str(), archiveInfoMap[path].m_offset,
                    archiveInfoMap[path].m_size)

                bytesWritten = stream.Write(lineStr.size(), lineStr.c_str());
                if (bytesWritten != lineStr.size())
                {
                    return AZ::Failure(AZStd::string("Failed to write the profiling logs\n"));
                }
            }

            lineStr = AZStd::string::format("**********\n");
            if (list.first == 0)
            {
                lineStr += AZStd::string::format("||||||||||  1000\n");
            }
            if (uCtr == infoMap.size())
                lineStr.clear();

            if (!lineStr.empty())
            {
                bytesWritten = stream.Write(lineStr.size(), lineStr.c_str());
                if (bytesWritten != lineStr.size())
                {
                    return AZ::Failure(AZStd::string("Failed to write the profiling logs\n"));
                }
            }
        }
        return AZ::Success();
    }

    template <typename T>
    void ConvertMapToPackIdKeyedMap(T assetIdMap, IdPackInfoListMap& packIdMap)
    {
        for (const auto& assetPackInfo : assetIdMap)
        {
            packIdMap[assetPackInfo.second.m_packId].emplace(assetPackInfo.second);
        }
    }

    AssetPackInfo::AssetPackInfo()
    {
    }

    AssetPackInfo::AssetPackInfo(const AZ::Data::AssetId& assetId, const AZStd::string& path, AZ::u32 packId)
        : m_assetId(assetId)
        , m_assetRelativePath(path)
        , m_packId(packId)
    {
    }

    AssetPackInfo::AssetPackInfo(const AZStd::string& path)
        : m_assetRelativePath(path)
    {
    }

    bool AssetPackInfo::operator==(const AssetPackInfo& rhs) const
    {
        return m_assetId == rhs.m_assetId && m_assetRelativePath == rhs.m_assetRelativePath && m_packId == rhs.m_packId;
    }

    bool AssetPackInfo::operator!=(const AssetPackInfo& rhs) const
    {
        return m_assetId != rhs.m_assetId || m_assetRelativePath != rhs.m_assetRelativePath || m_packId != rhs.m_packId;
    }

    void AssetPackInfo::JsonLoad(AZ::u32 packId, const JsonObject& jsonObject, AzFramework::PlatformFlags platformFlags)
    {
        bool bNoAssetId = false;
        bool bNoAssetHint = false;
        if (!jsonObject.HasMember(GuidKey) || !jsonObject.HasMember(SubIdKey))
            bNoAssetId = true;

        if (jsonObject.HasMember(AssetHintKey))
        {
            m_assetRelativePath = jsonObject.FindMember(AssetHintKey)->value.GetString();
        }
        else
        {
            bNoAssetHint = true;
        }
        
        if (bNoAssetId && !bNoAssetHint)
        {
            m_assetId = GetAssetIdByPath(m_assetRelativePath, platformFlags);
        }
        else
        {
            AZStd::string assetIdStr = AZStd::string::format(
                "%s:%x", jsonObject.FindMember(GuidKey)->value.GetString(), jsonObject.FindMember(SubIdKey)->value.GetUint());
            m_assetId = AZ::Data::AssetId::CreateString(assetIdStr);
        }

        if(bNoAssetHint && !bNoAssetId)
        {
            m_assetRelativePath = GetAssetPathById(m_assetId, platformFlags);
        }

        m_packId = packId;
    }

    AZStd::string AssetPackInfo::JsonStore(rapidjson::Value& outValue, rapidjson::Document::AllocatorType& allocator)
    {
        AZStd::string errStr("");

        if (!outValue.IsObject())
            outValue.SetObject();


        if (m_assetId.IsValid())
        {
            rapidjson::Value guidString;
            guidString.SetString(m_assetId.m_guid.ToString<AZStd::string>().c_str(), allocator);
            outValue.AddMember(rapidjson::StringRef(GuidKey), guidString, allocator);

            rapidjson::Value subId;
            subId.SetUint64(m_assetId.m_subId);
            outValue.AddMember(rapidjson::StringRef(SubIdKey), subId, allocator);
        }
        
        if (!m_assetRelativePath.empty())
        {
            rapidjson::Value pathString;
            pathString.SetString(m_assetRelativePath.c_str(), allocator);
            outValue.AddMember(rapidjson::StringRef(AssetHintKey), pathString, allocator);
        }

        if(!m_assetId.IsValid() && m_assetRelativePath.empty())
            errStr = "Storing this instance without a valid AssetId or a relative path...";

        return errStr;
    }
} // namespace BRAssetBundler
