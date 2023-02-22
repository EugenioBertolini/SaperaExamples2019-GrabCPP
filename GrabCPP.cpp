// Disable deprecated function warnings with Visual Studio 2005
#if defined(_MSC_VER) && _MSC_VER >= 1400
#pragma warning(disable: 4995)
#endif

// Includes
#include "stdio.h"
#include "conio.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <iomanip>

#include <stdexcept>
#include <vector>
#include <memory>
#include <atomic>

#include "SapClassBasic.h"
#include "ExampleUtils.h"

// Restore deprecated function warnings with Visual Studio 2005
#if defined(_MSC_VER) && _MSC_VER >= 1400
#pragma warning(default: 4995)
#endif

std::string fileName;

// SapMyProcessing Class
class SapMyProcessing : public SapProcessing
{
    public:
        SapMyProcessing(SapBuffer* pBuffers, SapProCallback pCallback, void* pContext);
        virtual ~SapMyProcessing();
    
    protected:
        virtual BOOL Run();
};

SapMyProcessing::SapMyProcessing(SapBuffer* pBuffers, SapProCallback pCallback, void* pContext) : SapProcessing(pBuffers, pCallback, pContext)
{ }

SapMyProcessing::~SapMyProcessing()
{
    if (m_bInitOK) Destroy();
}

BOOL SapMyProcessing::Run()
{
    // Get the current buffer index
    const int proIndex = GetIndex();
    
    // If this is not true, buffer has overflown
    SapBuffer::State state;
    bool goodContent = m_pBuffers->GetState(proIndex, &state) && state == SapBuffer::StateFull;
    
    if (goodContent) {
        void* inAddress = nullptr;
        m_pBuffers->GetAddress(proIndex, &inAddress);
        int inSize = 0;
        m_pBuffers->GetSpaceUsed(proIndex, &inSize);
        
        // Width, height and pixel format are received from the camera
        const int width = m_pBuffers->GetWidth();
        const int height = m_pBuffers->GetHeight();
        const auto format = m_pBuffers->GetFormat();
        const int outSize = width * height;
        
        // Skip unexpected pixel format or incomplete frame
        goodContent = format == SapFormatMono8 && inSize == outSize;
        
        if (goodContent) {
            // Copy data to vector
            std::vector<uint8_t> outBuffer(outSize);
            std::copy((uint8_t*)inAddress, (uint8_t*)(inAddress)+outSize, outBuffer.begin());

            // Open the output file stream in binary mode
            std::ofstream outputFile(fileName, std::ios::binary | std::ios::app);
            
            // Set the buffer size to 1MB for faster I/O
            outputFile.rdbuf()->pubsetbuf(nullptr, outSize);
            
            // Print to csvFile
            for (int i = 0; i < outSize; i += width) {
                // Write the data to the file
                outputFile.write(reinterpret_cast<char*>(&outBuffer[i]), width);
            }
            // Close the output file stream
            outputFile.close();
        }
    }
    
    return TRUE;
}

// Information to pass to callbacks
struct TransferContext
{
    std::atomic_int frameGrabCount = 0, frameProcessingCount = 0;
    std::shared_ptr<SapMyProcessing> processing;
};

void transferCallback(SapXferCallbackInfo* info)
{
    auto context = (TransferContext*)info->GetContext();
    
    context->frameGrabCount++;
    if (!info->IsTrash()) {
        // Execute Run() for this frame
        context->processing->ExecuteNext();
    }

    SapTransfer* pXfer = info->GetTransfer();
    static float lastframerate = 0.0f;
    if (pXfer->UpdateFrameRateStatistics()) {
      SapXferFrameRateInfo* pFrameRateInfo = pXfer->GetFrameRateStatistics();
      float framerate = 0.0f;

      if (pFrameRateInfo->IsLiveFrameRateAvailable())
          framerate = pFrameRateInfo->GetLiveFrameRate();

      // check if frame rate is stalled
      if (pFrameRateInfo->IsLiveFrameRateStalled()) {
          printf("Live frame rate is stalled.\n");
      }
      // update FPS only if the value changed by +/- 0.1
      else if ((framerate > 0.0f) && (abs(lastframerate - framerate) > 0.001f)) {
          printf("Grabbing at %.3f frames/sec\n", framerate);
          lastframerate = framerate;
      }
    }
}

// Processing callback is called after Run()
void processingCallback(SapProCallbackInfo* info)
{
    auto context = (TransferContext*)info->GetContext();
    
    // Processing has finished
    context->frameProcessingCount++;
}

// Static Functions
//static void XferCallback(SapXferCallbackInfo *pInfo);
static BOOL GetOptions(int argc, char *argv[], char *acqServerName, UINT32 *pAcqDeviceIndex, char *configFileName);
static BOOL GetOptionsFromCommandLine(int argc, char *argv[], char *acqServerName, UINT32 *pAcqDeviceIndex, char *configFileName);

// Main
int main(int argc, char* argv[])
{
    // Number of frames to receive from the camera
    const int maxFrameCount = 3000;

    // Configs
    UINT32 acqDeviceNumber;
    char* acqServerName = new char[CORSERVER_MAX_STRLEN];
    char* configFilename = new char[MAX_PATH];

    printf("Sapera Console Grab Example (C++ version)\n");

    // Call GetOptions to determine which acquisition device to use and which config file (CCF) should be loaded to configure it.
    if (!GetOptions(argc, argv, acqServerName, &acqDeviceNumber, configFilename)){
        printf("\nPress any key to terminate\n");
        CorGetch();
        return 0;
    }

    // Get the filename from the user at runtime
    std::cout << "Enter the name of the CSV file (w/out extension): ";
    std::cin >> fileName;
    std::string extension = ".bin";
    fileName.append(extension);

    // Struct to pass to callback
    TransferContext context;

    SapAcquisition Acq;
    SapAcqDevice AcqDevice;
    SapBuffer Buffers;

    std::unique_ptr<SapTransfer> Transfer;

    SapLocation loc(acqServerName, acqDeviceNumber);

    if (SapManager::GetResourceCount(acqServerName, SapManager::ResourceAcq) > 0) {
        Acq = SapAcquisition(loc, configFilename);
        Buffers = SapBuffer(maxFrameCount, &Acq);

        // Pass TransferContext struct to the transfer and processing callbacks
        Transfer = std::make_unique<SapAcqToBuf>(&Acq, &Buffers, transferCallback, &context);
        context.processing = std::make_shared<SapMyProcessing>(&Buffers, processingCallback, &context);

        // Create acquisition object
        if (!Acq.Create())
            goto FreeHandles;
    }

    else if (SapManager::GetResourceCount(acqServerName, SapManager::ResourceAcqDevice) > 0) {
        if (strcmp(configFilename, "NoFile") == 0)
            AcqDevice = SapAcqDevice(loc, FALSE);
        else
            AcqDevice = SapAcqDevice(loc, configFilename);

        Buffers = SapBuffer(maxFrameCount, &AcqDevice);

        // Pass TransferContext struct to the transfer and processing callbacks
        Transfer = std::make_unique<SapAcqDeviceToBuf>(&AcqDevice, &Buffers, transferCallback, &context);
        context.processing = std::make_shared<SapMyProcessing>(&Buffers, processingCallback, &context);

        // Create acquisition object
        if (!AcqDevice.Create())
            goto FreeHandles;
    }

    // Create buffer object
    if (!Buffers.Create())
        goto FreeHandles;

    // Create transfer object
    if (!Transfer->Create())
        goto FreeHandles;

    // Create processing object
    if (!context.processing->Create())
        goto FreeHandles;

    // Grab
    Transfer->Grab();

    // Wait for the camera to grab all frames
    while (context.frameGrabCount < maxFrameCount);

    // Stop grab
    Transfer->Freeze();
    if (!Transfer->Wait(5000))
        printf("Grab could not stop properly.\n");

    // Wait for processing to complete
    while (context.frameProcessingCount < maxFrameCount);

    FreeHandles:
    printf("Press any key to terminate\n");
    CorGetch();

    //unregister the acquisition callback
    Acq.UnregisterCallback();

    // Destroy processing object
    if (context.processing->Destroy()) return FALSE;

    // Destroy transfer object
    if (Transfer->Destroy()) return FALSE;

    // Destroy buffer object
    if (!Buffers.Destroy()) return FALSE;

    // Destroy acquisition object
    if (!Acq.Destroy()) return FALSE;

    // Destroy acquisition object
    if (!AcqDevice.Destroy()) return FALSE;

    return 0;
}


//static void XferCallback(SapXferCallbackInfo *pInfo)
//{
//    SapTransfer* pXfer = pInfo->GetTransfer();
//    if (pXfer->UpdateFrameRateStatistics())
//    {
//        SapXferFrameRateInfo* pFrameRateInfo = pXfer->GetFrameRateStatistics();
//        float framerate = 0.0f;
//
//        if (pFrameRateInfo->IsLiveFrameRateAvailable())
//            framerate = pFrameRateInfo->GetLiveFrameRate();
//
//        // check if frame rate is stalled
//        if (pFrameRateInfo->IsLiveFrameRateStalled()) {
//            printf("Live frame rate is stalled.\n");
//        }
//        // print FPS
//        printf("Grabbing at %.2f frames/sec\n", framerate);
//    }
//}

static BOOL GetOptions(int argc, char *argv[], char *acqServerName, UINT32 *pAcqDeviceIndex, char *configFileName)
{
    // Check if arguments were passed
    if (argc > 1)
        return GetOptionsFromCommandLine(argc, argv, acqServerName, pAcqDeviceIndex, configFileName);
    else
        return GetOptionsFromQuestions(acqServerName, pAcqDeviceIndex, configFileName);
}

static BOOL GetOptionsFromCommandLine(int argc, char *argv[], char *acqServerName, UINT32 *pAcqDeviceIndex, char *configFileName)
{
    // Check the command line for user commands
    if ((strcmp(argv[1], "/?") == 0) || (strcmp(argv[1], "-?") == 0)) {
        // print help
        printf("Usage:\n");
        printf("GrabCPP [<acquisition server name> <acquisition device index> <config filename>]\n");
        return FALSE;
    }

    // Check if enough arguments were passed
    if (argc < 4) {
        printf("Invalid command line!\n");
        return FALSE;
    }

    // Validate server name
    if (SapManager::GetServerIndex(argv[1]) < 0) {
        printf("Invalid acquisition server name!\n");
        return FALSE;
    }

    // Does the server support acquisition?
    int deviceCount = SapManager::GetResourceCount(argv[1], SapManager::ResourceAcq);
    int cameraCount = SapManager::GetResourceCount(argv[1], SapManager::ResourceAcqDevice);

    if (deviceCount + cameraCount == 0){
        printf("This server does not support acquisition!\n");
        return FALSE;
    }

    // Validate device index
    if (atoi(argv[2]) < 0 || atoi(argv[2]) >= deviceCount + cameraCount) {
        printf("Invalid acquisition device index!\n");
        return FALSE;
    }

    // Verify that the specified config file exist
    OFSTRUCT of = { 0 };
    if (OpenFile(argv[3], &of, OF_EXIST) == HFILE_ERROR) {
        printf("The specified config file (%s) is invalid!\n", argv[3]);
        return FALSE;
    }

    // Fill-in output variables
    CorStrncpy(acqServerName, argv[1], CORSERVER_MAX_STRLEN);
    *pAcqDeviceIndex = atoi(argv[2]);
    CorStrncpy(configFileName, argv[3], MAX_PATH);

    return TRUE;
}
