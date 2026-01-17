#include "audio_file_source_vfs.h"

#include <stdio.h>

AudioFileSourceVFS::AudioFileSourceVFS() : f_(nullptr) {}

AudioFileSourceVFS::AudioFileSourceVFS(const char* filename) : f_(nullptr)
{
    open(filename);
}

AudioFileSourceVFS::~AudioFileSourceVFS()
{
    close();
}

bool AudioFileSourceVFS::open(const char* filename)
{
    close();
    if (!filename || !*filename) {
        return false;
    }
    f_ = fopen(filename, "rb");
    return f_ != nullptr;
}

uint32_t AudioFileSourceVFS::read(void* data, uint32_t len)
{
    if (!f_ || !data || len == 0) {
        return 0;
    }
    return (uint32_t)fread(data, 1, len, f_);
}

bool AudioFileSourceVFS::seek(int32_t pos, int dir)
{
    if (!f_) {
        return false;
    }
    return fseek(f_, pos, dir) == 0;
}

bool AudioFileSourceVFS::close()
{
    if (!f_) {
        return false;
    }
    fclose(f_);
    f_ = nullptr;
    return true;
}

bool AudioFileSourceVFS::isOpen()
{
    return f_ != nullptr;
}

uint32_t AudioFileSourceVFS::getSize()
{
    if (!f_) {
        return 0;
    }
    long pos = ftell(f_);
    if (pos < 0) {
        return 0;
    }
    if (fseek(f_, 0, SEEK_END) != 0) {
        return 0;
    }
    long size = ftell(f_);
    fseek(f_, pos, SEEK_SET);
    if (size < 0) {
        return 0;
    }
    return (uint32_t)size;
}

uint32_t AudioFileSourceVFS::getPos()
{
    if (!f_) {
        return 0;
    }
    long pos = ftell(f_);
    if (pos < 0) {
        return 0;
    }
    return (uint32_t)pos;
}
