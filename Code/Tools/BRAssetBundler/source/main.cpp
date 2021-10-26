#include <source/applicationManager.h>

int main(int argc, char* argv[])
{

    AZ::AllocatorInstance<AZ::SystemAllocator>::Create();
    int runSuccess = 0;

    {
        //This nested scope is necessary as the applicationManager needs to have its destructor called BEFORE you destroy the allocators
        BRAssetBundler::ApplicationManager applicationManager(&argc, &argv);
        if (!applicationManager.Init())
        {
            AZ_Error("AssetBundler", false, "AssetBundler initialization failed");
            runSuccess = 1;
        }
        else
        {
            runSuccess = applicationManager.Run() ? 0 : 1;
        }
    }

    AZ::AllocatorInstance<AZ::SystemAllocator>::Destroy();
    return runSuccess;
}
