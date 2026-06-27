#include <iostream>
#include <vector>
#include "OptimFROG.h"

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    std::cout << "OptimFROGcpp Decoder v" << OptimFROG_getVersion() << std::endl;
    
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <input.ofr>" << std::endl;
        return 1;
    }
    
    void* instance = OptimFROG_createInstance();
    if (!instance) {
        std::cerr << "Failed to create instance!" << std::endl;
        return 1;
    }
    
    if (OptimFROG_open(instance, argv[1]) != OptimFROG_NoError) {
        std::cerr << "Failed to open file: " << argv[1] << std::endl;
        return 1;
    }
    
    OptimFROG_Info info;
    if (OptimFROG_getInfo(instance, &info)) {
        std::cout << "--- OFR File Info ---" << std::endl;
        std::cout << "Sample Rate: " << info.samplerate << " Hz" << std::endl;
        std::cout << "Channels: " << info.channels << std::endl;
        std::cout << "Bit Depth: " << info.bitspersample << " bits" << std::endl;
        std::cout << "Total Samples: " << (info.length_ms * info.samplerate / 1000) << std::endl;
    }
    
    std::cout << "Attempting to decode..." << std::endl;
    // OptimFROG_read fills the buffer in the native sample format: bitspersample/8
    // bytes per value (1/2/3/4). Size the buffer for the widest case and write the
    // native byte width so 8/24/32-bit output is byte-exact (not just 16-bit).
    const uint32_t bytesPerValue = info.bitspersample / 8;
    std::vector<uint8_t> buffer(info.channels * 4096 * 4);
    int total_read = 0;
    FILE* out_f = fopen("test.raw", "wb");
    while (true) {
        int read = OptimFROG_read(instance, buffer.data(), 4096);
        if (read <= 0) break;
        total_read += read;
        if (out_f) fwrite(buffer.data(), bytesPerValue, read * info.channels, out_f);
    }
    if (out_f) fclose(out_f);
    std::cout << "Decoded " << total_read << " samples." << std::endl;
    
    OptimFROG_destroyInstance(instance);
    return 0;
}
