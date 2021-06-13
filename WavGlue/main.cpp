#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <exception>
#include <utility>
#include <sstream>
#include <boost/program_options.hpp>

using namespace std;
namespace po = boost::program_options;

const unsigned int wavHeaderLength = 44;

struct WavHeader {
    uint32_t riff = 1179011410;
    uint32_t offRiffSize = 36;
    uint32_t riffType = 1163280727;
    uint32_t fmt = 544501094;
    uint32_t fmtSize = 16;
    uint16_t wavType = 1;
    uint16_t channels = 1;
    uint32_t sampleRate = 8000;
    uint32_t byteRate = 16000;
    uint16_t blockAlign = 2;
    uint16_t bitsPerSample = 16;
    uint32_t dataBlock = 1635017060;
    uint32_t dataBlockSize = 8;
};

class WavFile {
private:
    string filePath;
public:
    WavHeader header;
    fstream dataStream;

    WavFile(const string& filePath) {
        dataStream = fstream(filePath, fstream::binary | fstream::in | fstream::out);
        if (dataStream) {
            this->filePath = filePath;
            dataStream.read((char*)&header, wavHeaderLength);
        }
        else {
            cout << "File at " << filePath << " not found! Creating new one" << endl;
            ofstream creator(filePath);
            creator.close();
            header = WavHeader();
            dataStream = fstream(filePath, fstream::binary | fstream::in | fstream::out);
            this->filePath = filePath;
            SaveHeader();
        }
    }

    ~WavFile() {
        dataStream.close();
    }

    string GetFilePath() const {
        return filePath;
    }

    unsigned int GetChunksPerChannel() const {
        return (header.dataBlockSize - 8) / (header.channels * GetBytesPerSample());
    }

    unsigned int GetBytesPerSample() const {
        return header.bitsPerSample / 8;
    }

    unsigned int GetDuration() const {
        return GetChunksPerChannel() / header.sampleRate;
    }

    vector<char> GetDataFromChannel(const short channelNumber, const unsigned int samplesCount) {
        unsigned int bytesPerSample = GetBytesPerSample();
        vector<char> bytes(samplesCount * bytesPerSample);
        for (int i = 0; i < samplesCount; ++i) {
            if (dataStream.eof() || dataStream.fail()) {
                dataStream.clear();
                break;
            }
            fstream::pos_type pos = wavHeaderLength + bytesPerSample * channelNumber + bytesPerSample * i * header.channels;
            dataStream.seekg(pos, dataStream.beg);
            char* buffer = new char[bytesPerSample];
            dataStream.read(buffer, bytesPerSample);
            for (int j = 0; j < bytesPerSample; ++j) {
                bytes[(i * bytesPerSample) + j] = buffer[j];
            }
            delete[] buffer;
        }
        return bytes;
    }
    void SetDataOnChannel(const short channelNumber, vector<char>& bytes) {
        unsigned int bytesPerSample = GetBytesPerSample();
        unsigned int samplesCount = bytes.size() / bytesPerSample;
        for (int i = 0; i < samplesCount; ++i) {
            if (dataStream.eof() || dataStream.fail()) {
                dataStream.clear();
                break;
            }
            fstream::pos_type pos = wavHeaderLength + bytesPerSample * channelNumber + bytesPerSample * i * header.channels;
            dataStream.seekp(pos, dataStream.beg);
            char* buffer = new char[bytesPerSample];
            for (int j = 0; j < bytesPerSample; ++j) {
                buffer[j] = bytes[(i * bytesPerSample) + j];
            }
            dataStream.write(buffer, bytesPerSample);
            delete[] buffer;
        }
    }
    void ClearChannel(short channelNumber) {
        unsigned int samplesCount = GetChunksPerChannel();
        unsigned int bytesPerSample = GetBytesPerSample();
        dataStream.seekp(0, dataStream.beg);
        for (int i = 0; i < samplesCount; ++i) {
            if (dataStream.eof() || dataStream.fail()) {
                dataStream.clear();
                break;
            }
            dataStream.seekp(wavHeaderLength + bytesPerSample * channelNumber + bytesPerSample * i * header.channels, dataStream.beg);
            char* zeros = new char[bytesPerSample];
            for (int j = 0; j < bytesPerSample; ++j) {
                zeros[j] = 0;
            }
            dataStream.write(zeros, bytesPerSample);
            delete[] zeros;
        }
    }
    void ClearAllChannels() {
        for (int i = 0; i < header.channels; ++i) {
            ClearChannel(i);
        }
    }
    void SaveHeader() {
        dataStream.seekp(0, dataStream.beg);
        dataStream.write((char*)&header, wavHeaderLength);
    }
};

class WavFileValidator {
private:
    WavHeader expectedHeader;

    template<typename T>
    bool CheckField(const T& toValidate, const T& expected, const string& fieldName = "header field") {
        if (toValidate != expected) {
            ostringstream ss;
            ss << "WAVE PCM " << fieldName << " not validated: expected {" << expected << "}, was {" << toValidate << "}!";
            throw invalid_argument(ss.str());
            return false;
        }
        return true;
    }

public:
    WavFileValidator() : expectedHeader(WavHeader()) {}

    bool Validate(const WavFile& file) {
        try
        {
            bool validated = true;
            validated = validated &
            CheckField<uint32_t>(file.header.riff, expectedHeader.riff, "RIFF") &
            CheckField<uint32_t>(file.header.riffType, expectedHeader.riffType, "RIFF type") &
            CheckField<uint16_t>(file.header.wavType, expectedHeader.wavType, "WAVE type") &
            CheckField<uint16_t>(file.header.channels, expectedHeader.channels, "channels count") &
            CheckField<uint32_t>(file.header.sampleRate, expectedHeader.sampleRate, "sample rate")&
            CheckField<uint16_t>(file.header.bitsPerSample, expectedHeader.bitsPerSample, "bits per sample");
            return validated;
        }
        catch (const std::exception& e)
        {
            ostringstream ss;
            ss << "In file " << file.GetFilePath() << ":\n" << e.what() << endl;
            throw invalid_argument(ss.str());
        }
        return false;
    }
};

class WavEditor {
public:
    void Combine(const WavFile& firstFile, const WavFile& secondFile, WavFile& result) {

        WavFile* firstFilePtr = const_cast<WavFile*>(&firstFile);
        WavFile* secondFilePtr = const_cast<WavFile*>(&secondFile);

        if (secondFile.header.dataBlockSize > firstFile.header.dataBlockSize) {
            firstFilePtr = const_cast<WavFile*>(&secondFile);
            secondFilePtr = const_cast<WavFile*>(&firstFile);
        }

        WavFile& first = *(firstFilePtr);
        WavFile& second = *(secondFilePtr);

        uint16_t channels = 2;
        uint32_t size = first.header.offRiffSize * 2;
        uint32_t dataBlockSize = first.header.dataBlockSize * 2;
        WavHeader& resultHeader = result.header;
        resultHeader.dataBlockSize = dataBlockSize;
        resultHeader.sampleRate = first.header.sampleRate;
        resultHeader.channels = channels;
        resultHeader.bitsPerSample = first.header.bitsPerSample;
        resultHeader.offRiffSize = size;
        result.SaveHeader();
        vector<char> firstFileData = first.GetDataFromChannel(0, first.GetChunksPerChannel());
        vector<char> secondFileData = second.GetDataFromChannel(0, second.GetChunksPerChannel());
        result.SetDataOnChannel(0, firstFileData);
        result.SetDataOnChannel(1, secondFileData);
    }
    void MultiplyVolume(WavFile& file, const float factor = 1) {
        if (factor < 0 || factor > 2) {
            throw invalid_argument("Multiply factor must be in range from 0 to 2!");
        }
        if (factor == 1) {
            return;
        }
        unsigned int chunksPerChannel = file.GetChunksPerChannel();
        unsigned int bytesPerSample = file.GetBytesPerSample();
        vector<char> leftChannelBytes = file.GetDataFromChannel(0, chunksPerChannel);
        vector<char> rightChannelBytes = file.GetDataFromChannel(1, chunksPerChannel);
        for (int i = 0; i < chunksPerChannel; ++i) {
            char* bytes = new char[bytesPerSample];
            for (int j = 0; j < bytesPerSample; ++j) {
                bytes[j] = leftChannelBytes[(i * bytesPerSample) + j];
            }
            int16_t resultSample = (float)*((int16_t*)bytes) * factor;
            char* rawResultSample = (char*)&resultSample;
            for (int j = 0; j < bytesPerSample; ++j) {
                leftChannelBytes[(i * bytesPerSample) + j] = rawResultSample[j];
            }
            delete[] bytes;
        }
        for (int i = 0; i < chunksPerChannel; ++i) {
            char* bytes = new char[bytesPerSample];
            for (int j = 0; j < bytesPerSample; ++j) {
                bytes[j] = rightChannelBytes[(i * bytesPerSample) + j];
            }
            int16_t resultSample = (float)*((int16_t*)bytes) * factor;
            char* rawResultSample = (char*)&resultSample;
            for (int j = 0; j < bytesPerSample; ++j) {
                rightChannelBytes[(i * bytesPerSample) + j] = rawResultSample[j];
            }
            delete[] bytes;
        }

        file.SetDataOnChannel(0, leftChannelBytes);
        file.SetDataOnChannel(1, rightChannelBytes);
    }
};

void LogWav(const WavFile& file) {
    cout << "File at " << file.GetFilePath() << endl;
    const WavHeader& header = file.header;
    cout << "RIFF type: " << string((char*)&header.riffType).substr(0, 4) << endl;
    cout << "WAVE type: " << header.wavType << endl;
    cout << "Channels count: " << header.channels << endl;
    cout << "Sample rate: " << header.sampleRate << " Hz" << endl;
    cout << "Bits per sample: " << header.bitsPerSample << endl;
    cout << "Data size: " << header.dataBlockSize << " bytes" << endl;
    cout << "Total duration: " << file.GetDuration() << " seconds" << endl;
    cout << "---------" << endl;
}

int main(int argc, char** argv) {

    string firstFilePath;
    string secondFilePath;
    string resultFilePath;
    float multiplyFactor;

    po::options_description description("Allowed options");
    description.add_options()
        ("help,h", "Show help")
        ("leftchannel,l", po::value<string>(&firstFilePath)->value_name("path"), "Path to mono WAVE PCM file for left channel")
        ("rightchannel,r", po::value<string>(&secondFilePath)->value_name("path"), "Path to mono WAVE PCM file for right channel")
        ("volumemultiplier,m", po::value<float>(&multiplyFactor)->default_value(1.0f)->value_name("m"), "Volume multiplier for result file")
        ("output,o", po::value<string>(&resultFilePath)->value_name("path"), "Path for output stereo WAVE PCM file (file will be created)")
        ;

    po::variables_map vm;

    try
    {
        po::store(po::parse_command_line(argc, argv, description), vm);
        po::notify(vm);
    }
    catch (const std::exception& e)
    {
        cerr << e.what() << endl;
        exit(3);
    }

    if (vm.count("help")) {
        cout << description << endl;
        return 0;
    }
    
    try {

        if (firstFilePath.empty() || secondFilePath.empty() || resultFilePath.empty()) {
            throw invalid_argument("Missing arguments!");
        }

        ifstream checker(firstFilePath);
        if (!checker) {
            checker.close();
            throw invalid_argument("First file not found!");
        }
        checker.close();
        cout << "First file is " << firstFilePath << endl;

        checker.open(secondFilePath);
        if (!checker) {
            checker.close();
            throw invalid_argument("Second file not found!");
        }
        checker.close();
        cout << "Second file is " << secondFilePath << endl;

        cout << "Multiplier is " << multiplyFactor << endl;

        cout << endl;

        WavFileValidator fileValidator;

        WavFile firstFile(firstFilePath);
        WavFile secondFile(secondFilePath);

        bool pass = true;
        pass = pass & fileValidator.Validate(firstFile);
        pass = pass & fileValidator.Validate(secondFile);

        LogWav(firstFile);
        LogWav(secondFile);
        cout << endl;

        WavFile resultFile(resultFilePath);

        WavEditor editor;
        cout << "Channels combining..." << endl;
        editor.Combine(firstFile, secondFile, resultFile);
        cout << "Managing volume..." << endl;
        editor.MultiplyVolume(resultFile, multiplyFactor);

        cout << endl;
        LogWav(resultFile);
        cout << "Done!" << endl;
    }
    catch (const exception& e) {
        cerr << e.what() << endl;
        cout << "Use flag -h to get help" << endl;
        exit(2);
    }
    return 0;
}
