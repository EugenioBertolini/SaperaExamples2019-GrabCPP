// Disable deprecated function warnings with Visual Studio 2005
#if defined(_MSC_VER) && _MSC_VER >= 1400
#pragma warning(disable: 4995)
#endif

// Includes
#include "stdio.h"
#include "conio.h"
#include "direct.h"
#include "json.hpp"

#include <chrono>
#include <ctime>
#include <filesystem>

#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <string>
#include <iomanip>

#include <stdexcept>
#include <vector>
#include <memory>
#include <atomic>

#include <cstdio>
#include <cstdint>
#include <cstring>

#include "SapClassBasic.h"
#include "ExampleUtils.h"

using json = nlohmann::json;
using fs_path = std::filesystem::path;

// Restore deprecated function warnings with Visual Studio 2005
#if defined(_MSC_VER) && _MSC_VER >= 1400
#pragma warning(default: 4995)
#endif

fs_path dataFileName;
fs_path fpsFileName;

class ExperimentInfo {
private:
    int experiment_id; // experiment type, e.g. "walk forward", "reverse", "reproduction", "food search"...
    int arena_id; // arena shape, size, holes pattern
    int trial; // number that increments for each trial on the same fly

    int fly_id; // number unique for each fly
    int genetic_line;
    int sex;
    int age; // days from hatching

    void getValue(int& value, std::string out) {
        std::string input;
        std::cout << out;
        getline(std::cin, input);
        if (!input.empty()) {
            value = std::stoi(input);
        }
    }

public:
    ExperimentInfo() {
        experiment_id = 0; // 0 = classic arena walking
        arena_id = 0; // 0 = classic oval arena with grid pattern
        trial = 0;
        fly_id = 0;
        genetic_line = 0; // 0 = Wild Dickinson
        age = 3;
        sex = 0; // 0 = male | 1 = female
    }

    void inputValues() {
        std::cout << std::endl;
        getValue(experiment_id,   std::format("Input the experiment_id (default is {}): |", experiment_id));
        getValue(arena_id,        std::format("Input the arena_id (default is {}): |", arena_id));
        getValue(trial,           std::format("Input the trial number (default is trial number {}): |", trial));
        getValue(fly_id,          std::format("Input the fly_id of the fly (default is {}): |", fly_id));
        getValue(genetic_line,    std::format("Input the genetic_line of the fly [0 -> Wild Dickinson] (default is {}): |", genetic_line));
        getValue(age,             std::format("Input the age of the fly in days (default is {}): |", age));
        getValue(sex,             std::format("Input the sex of the fly [0 -> Male, 1 -> Female] (default is {}): |", sex));
    }

    void printValues() {
        std::cout << std::endl;
        std::cout << "experiment_id: " << experiment_id << std::endl;
        std::cout << "arena_id:      " << arena_id << std::endl;
        std::cout << "trial:         " << trial << std::endl;
        std::cout << "fly_id:        " << fly_id << std::endl;
        std::cout << "genetic_line:  " << genetic_line << std::endl;
        std::cout << "age:           " << age << std::endl;
        std::cout << "sex:           " << sex << std::endl << std::endl;
    }

    int get_experiment_id() {
        return experiment_id;
    }
    int get_arena_id() {
        return arena_id;
    }
    int get_trial() {
        return trial;
    }
    int get_fly_id() {
        return fly_id;
    }
    int get_genetic_line() {
        return genetic_line;
    }
    int get_age() {
        return age;
    }
    int get_sex() {
        return sex;
    }
};


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
            std::ofstream outputFile(dataFileName, std::ios::binary | std::ios::app);
            
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
    std::atomic_int frameGrabCount = 0;
    std::atomic_int frameProcessingCount = 0;
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
        // Open the output file stream in binary mode
        std::ofstream outputFile(fpsFileName, std::ios::binary | std::ios::app);
        // Write the data to the file
        outputFile.write(reinterpret_cast<char*>(&framerate), sizeof(float));
        // Close the output file stream
        outputFile.close();
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

    // Time now
    auto now = std::chrono::system_clock::now();
    std::time_t current_time = std::chrono::system_clock::to_time_t(now);

    // Convert current system time to struct tm in local time
    struct std::tm local_time;
    localtime_s(&local_time, &current_time);

    // Create folder with current time as name
    std::ostringstream oss;
    oss << std::put_time(&local_time, "%y-%m-%d-%H-%M-%S");
    fs_path folder_name = oss.str();
    std::filesystem::create_directory("test");
    folder_name = "test" / folder_name;
    std::filesystem::create_directory(folder_name);

    // Print folder name
    std::cout << std::endl << "Folder created: " << folder_name << std::endl;
    dataFileName = folder_name / "data.bin";
    fpsFileName = folder_name / "fps.bin";

    // Create the info class and get the JSON object with data
    ExperimentInfo expInfoObj;
    expInfoObj.inputValues();
    std::cout << "time: " << oss.str() << std::endl;
    std::cout << "frame_count: " << maxFrameCount << std::endl << std::endl;
    expInfoObj.printValues();
    
    // Add more fields to JSON object
    json jdata;
    jdata["time"] = oss.str();
    jdata["frame_count"] = maxFrameCount;
    jdata["data_path"] = dataFileName;
    jdata["fps_path"] = fpsFileName;
    
    jdata["experiment_id"] = expInfoObj.get_experiment_id();
    jdata["arena_id"] = expInfoObj.get_arena_id();
    jdata["trial"] = expInfoObj.get_trial();
    jdata["fly_id"] = expInfoObj.get_fly_id();
    jdata["genetic_line"] = expInfoObj.get_genetic_line();
    jdata["age"] = expInfoObj.get_age();
    jdata["sex"] = expInfoObj.get_sex();

    // Write JSON object to file
    fs_path jsonFileName = folder_name / "info.json";
    std::ofstream outfile(jsonFileName);
    if (outfile.is_open()) {
        outfile << std::setw(4) << jdata << std::endl;
        outfile.close();
        std::cout << std::endl << "Data written to .JSON file." << std::endl;
    }

    // Append data to file
    std::ofstream outcsv("./test/all_info.csv", std::ios::app);
    if (!outcsv.is_open()) {
        // File not found, create new file
        outcsv.open("./test/all_info.csv", std::ios::out);
        if (!outcsv.is_open()) {
            std::cerr << std::endl << "Error: Could not create all_info.csv" << std::endl;
            return 0;
        }
        outcsv << "datetime,frame_count,data_file_name,fps_file_name,experiment_id,arena_id,trial,fly_id,genetic_line,age,sex" << std::endl;
    }
    outcsv << oss.str() << "," << maxFrameCount << "," << dataFileName << "," << fpsFileName << "," <<
        expInfoObj.get_experiment_id() << "," << expInfoObj.get_arena_id() << "," << expInfoObj.get_trial() << "," <<
        expInfoObj.get_fly_id() << "," << expInfoObj.get_genetic_line() << "," << expInfoObj.get_age() << "," << expInfoObj.get_sex() << std::endl;
    outcsv.close();
    std::cout << std::endl << "Data appended to .CSV file." << std::endl << std::endl;

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
