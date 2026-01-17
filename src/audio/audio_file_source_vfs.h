#pragma once

#include <AudioFileSource.h>

class AudioFileSourceVFS : public AudioFileSource
{
public:
    AudioFileSourceVFS();
    explicit AudioFileSourceVFS(const char* filename);
    ~AudioFileSourceVFS() override;

    bool open(const char* filename) override;
    uint32_t read(void* data, uint32_t len) override;
    bool seek(int32_t pos, int dir) override;
    bool close() override;
    bool isOpen() override;
    uint32_t getSize() override;
    uint32_t getPos() override;

private:
    FILE* f_;
};
