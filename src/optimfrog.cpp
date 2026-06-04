#include "OptimFROG.h"
#include "../include/optimfrog_decoder.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>

extern "C" {

uInt32_t OptimFROG_getVersion(void) {
    return 5100;
}

void* OptimFROG_createInstance(void) {
    auto* instance = new OptimFROG_InternalState();
    memset(instance, 0, sizeof(OptimFROG_InternalState));
    return instance;
}

void OptimFROG_destroyInstance(void* decoderInstance) {
    if (decoderInstance) {
        auto* instance = static_cast<OptimFROG_InternalState*>(decoderInstance);
        if (instance->pInterface) {
            instance->pInterface->close();
            delete instance->pInterface;
        }
        OptimFROG_freeTags(&instance->tags);
        delete instance;
    }
}

condition_t OptimFROG_openExt(void* decoderInstance, ReadInterface* rInt, void* readerInstance, condition_t readTags) {
    auto* instance = static_cast<OptimFROG_InternalState*>(decoderInstance);
    instance->is_opened = false;
    instance->pInterface = nullptr;

    OFR_DecoderEngine* pOVar7 = nullptr;

    if (rInt->seekable(readerInstance) == C_FALSE) {
        pOVar7 = new OFR_DecoderEngine();
        instance->pInterface = pOVar7;
    } else {
        sInt64_t pos = rInt->getPos(readerInstance);
        char magic = 0;
        sInt32_t bytesRead = rInt->read(readerInstance, &magic, 1);
        rInt->seek(readerInstance, pos);

        if (bytesRead != 1 || magic != '*') {
            pOVar7 = new OFR_DecoderEngine();
            instance->pInterface = pOVar7;
        } else {
            pOVar7 = new OFR_DecoderEngine();
            instance->pInterface = pOVar7;
        }
    }

    ReadInterfaceWrapper wrapper = {rInt, readerInstance};
    if (!pOVar7->open(&wrapper)) {
        delete pOVar7;
        instance->pInterface = nullptr;
        return C_FALSE;
    } else {
        uint32_t headSize = pOVar7->headSize;
        instance->noPoints = pOVar7->total_samples / pOVar7->channels;
        instance->originalSize = (pOVar7->bitspersample / 8) * pOVar7->total_samples + headSize;

        sInt64_t compSize = 0;
        if (rInt->length) {
            compSize = rInt->length(readerInstance);
        }
        instance->compressedSize = compSize;

        double dVar2 = 1000.0;
        uint32_t samplerate = pOVar7->samplerate;
        instance->length_ms = (sInt64_t)(((double)instance->noPoints * dVar2) / (double)samplerate);

        if (instance->originalSize == 0 || instance->compressedSize == 0) {
            instance->bitrate = (sInt32_t)(sInt64_t)((double)(samplerate * pOVar7->bitspersample * pOVar7->channels) / dVar2);
        } else {
            instance->bitrate = (sInt32_t)(sInt64_t)((((double)instance->compressedSize / (double)instance->originalSize) * 
                                                  (double)(samplerate * pOVar7->bitspersample * pOVar7->channels)) / dVar2);
        }

        instance->has_tags = C_FALSE;
        if (readTags != C_FALSE) {
            // Read tags logic placeholder
            instance->has_tags = C_TRUE;
        }

        instance->points_read_so_far = 0;
        instance->is_opened = true;
        instance->tail_read = false;
        return C_TRUE;
    }
}

condition_t OptimFROG_open(void* decoderInstance, char* fileName, condition_t readTags) {
    return OptimFROG_NoError;
}

condition_t OptimFROG_close(void* decoderInstance) {
    auto* instance = static_cast<OptimFROG_InternalState*>(decoderInstance);
    if (!instance || !instance->is_opened) return OptimFROG_NoError;
    if (instance->pInterface) {
        instance->pInterface->close();
        delete instance->pInterface;
        instance->pInterface = nullptr;
    }
    instance->is_opened = false;
    return OptimFROG_NoError;
}

sInt32_t OptimFROG_readHead(void* decoderInstance, void* headData, uInt32_t maxSize) {
    auto* instance = static_cast<OptimFROG_InternalState*>(decoderInstance);
    if (!instance || !instance->pInterface) return 0;
    auto* pInt = instance->pInterface;
    if (pInt->headSize <= maxSize && pInt->headSize != 0) {
        memcpy(headData, pInt->headData, pInt->headSize);
    }
    return pInt->headSize;
}

sInt32_t OptimFROG_readTail(void* decoderInstance, void* tailData, uInt32_t maxSize) {
    auto* instance = static_cast<OptimFROG_InternalState*>(decoderInstance);
    if (!instance || !instance->pInterface) return -1;
    auto* pInt = instance->pInterface;
    
    if (instance->points_read_so_far != instance->noPoints) {
        return -1;
    }
    
    if (!instance->tail_read) {
        bool res = pInt->readTail();
        instance->tail_read = true;
        if (!res) return 0;
    }
    
    if (pInt->tailSize > maxSize) return -1;
    
    if (pInt->tailSize != 0 && pInt->tailData != nullptr) {
        memcpy(tailData, pInt->tailData, pInt->tailSize);
    }
    return pInt->tailSize;
}

condition_t OptimFROG_seekable(void* decoderInstance) {
    auto* instance = static_cast<OptimFROG_InternalState*>(decoderInstance);
    if (!instance || !instance->pInterface) return C_FALSE;
    return C_TRUE;
}

condition_t OptimFROG_seekPoint(void* decoderInstance, sInt64_t point) {
    auto* instance = static_cast<OptimFROG_InternalState*>(decoderInstance);
    if (!instance || !instance->is_opened || !instance->pInterface) return C_FALSE;
    
    auto* pInt = instance->pInterface;
    sInt64_t sample_pos = point * pInt->channels;
    
    if (sample_pos < pInt->total_samples) {
        if (pInt->seek(sample_pos)) {
            instance->points_read_so_far = sample_pos;
            return C_TRUE;
        }
    }
    
    instance->points_read_so_far = pInt->total_samples;
    return C_FALSE;
}

condition_t OptimFROG_seekTime(void* decoderInstance, sInt64_t milliseconds) {
    auto* instance = static_cast<OptimFROG_InternalState*>(decoderInstance);
    if (!instance || !instance->pInterface) return C_FALSE;
    sInt64_t point = (sInt64_t)((double)milliseconds / 1000.0 * instance->pInterface->samplerate);
    return OptimFROG_seekPoint(decoderInstance, point);
}

sInt64_t OptimFROG_getPos(void* decoderInstance) {
    auto* instance = static_cast<OptimFROG_InternalState*>(decoderInstance);
    if (!instance || !instance->pInterface || instance->pInterface->channels == 0) return 0;
    return instance->points_read_so_far / instance->pInterface->channels;
}

condition_t OptimFROG_recoverableErrors(void* decoderInstance) {
    auto* instance = static_cast<OptimFROG_InternalState*>(decoderInstance);
    if (!instance || !instance->pInterface) return C_FALSE;
    return instance->pInterface->has_recoverable_errors ? C_TRUE : C_FALSE;
}

sInt32_t OptimFROG_read(void* decoderInstance, void* data, uInt32_t noPoints, condition_t max16bit) {
    auto* instance = static_cast<OptimFROG_InternalState*>(decoderInstance);
    if (!instance || !instance->is_opened || !instance->pInterface) return -1;
    
    auto* pInt = instance->pInterface;
    
    sInt64_t samples_to_read = noPoints * pInt->channels;
    sInt64_t samples_left = pInt->total_samples - instance->points_read_so_far;
    
    if (samples_to_read > samples_left) {
        samples_to_read = samples_left;
    }
    
    if (samples_to_read == 0) return 0;
    
    int* temp_buffer = (int*)malloc(samples_to_read * sizeof(int));
    if (!temp_buffer) return -1;
    
    pInt->read(temp_buffer, samples_to_read);
    
    int stype = pInt->sample_type;
    
    if (stype == 0 || stype == 1) { // 8 bit
        for (sInt64_t i = 0; i < samples_to_read; ++i) {
            ((uint8_t*)data)[i] = (stype == 0 ? 0x80 : 0) + temp_buffer[i];
        }
    } else if (stype == 2 || stype == 3) { // 16 bit
        int offset = (stype == 2) ? 0x8000 : 0;
        for (sInt64_t i = 0; i < samples_to_read; ++i) {
            int val = temp_buffer[i] + offset;
            ((uint8_t*)data)[i*2] = val & 0xFF;
            ((uint8_t*)data)[i*2+1] = (val >> 8) & 0xFF;
        }
    } else if (stype == 4 || stype == 5) { // 24 bit
        int offset = (stype == 4) ? 0x800000 : 0;
        if (!max16bit) {
            for (sInt64_t i = 0; i < samples_to_read; ++i) {
                int val = temp_buffer[i] + offset;
                ((uint8_t*)data)[i*3] = val & 0xFF;
                ((uint8_t*)data)[i*3+1] = (val >> 8) & 0xFF;
                ((uint8_t*)data)[i*3+2] = (val >> 16) & 0xFF;
            }
        } else {
            for (sInt64_t i = 0; i < samples_to_read; ++i) {
                int val = temp_buffer[i] + offset;
                ((uint8_t*)data)[i*2] = (val >> 8) & 0xFF;
                ((uint8_t*)data)[i*2+1] = (val >> 16) & 0xFF;
            }
        }
    } else if (stype == 6 || stype == 7) { // 32 bit
        int offset = (stype == 6) ? 0x80000000 : 0;
        if (!max16bit) {
            for (sInt64_t i = 0; i < samples_to_read; ++i) {
                int val = temp_buffer[i] + offset;
                ((uint8_t*)data)[i*4] = val & 0xFF;
                ((uint8_t*)data)[i*4+1] = (val >> 8) & 0xFF;
                ((uint8_t*)data)[i*4+2] = (val >> 16) & 0xFF;
                ((uint8_t*)data)[i*4+3] = (val >> 24) & 0xFF;
            }
        } else {
            for (sInt64_t i = 0; i < samples_to_read; ++i) {
                int val = temp_buffer[i] + offset;
                ((uint8_t*)data)[i*2] = (val >> 16) & 0xFF;
                ((uint8_t*)data)[i*2+1] = (val >> 24) & 0xFF;
            }
        }
    } else {
        free(temp_buffer);
        return -1;
    }
    
    free(temp_buffer);
    instance->points_read_so_far += samples_to_read;
    return samples_to_read / pInt->channels;
}

condition_t OptimFROG_getInfo(void* decoderInstance, OptimFROG_Info* info) {
    auto* instance = static_cast<OptimFROG_InternalState*>(decoderInstance);
    if (!instance || !instance->is_opened || !instance->pInterface) return C_FALSE;
    auto* pInt = instance->pInterface;
    
    info->channels = pInt->channels;
    info->samplerate = pInt->samplerate;
    info->bitspersample = pInt->bitspersample;
    info->bitrate = instance->bitrate;
    info->version = pInt->version;
    info->method = "fast"; // simplified placeholder for now
    info->speedup = "normal"; // simplified placeholder for now
    info->noPoints = instance->noPoints;
    info->originalSize = instance->originalSize;
    info->compressedSize = instance->compressedSize;
    info->length_ms = instance->length_ms;
    info->sampleType = "signed 16-bit"; // placeholder
    info->channelConfig = "stereo"; // placeholder
    
    return C_TRUE;
}

condition_t OptimFROG_getTags(void* decoderInstance, OptimFROG_Tags* tags) {
    auto* instance = static_cast<OptimFROG_InternalState*>(decoderInstance);
    if (!instance || !instance->is_opened || !instance->has_tags) {
        if (tags) tags->keyCount = 0;
        return C_FALSE;
    }
    
    tags->keyCount = instance->tags.keyCount;
    for (uInt32_t i = 0; i < tags->keyCount; ++i) {
        tags->keys[i] = strdup(instance->tags.keys[i]);
        tags->values[i] = strdup(instance->tags.values[i]);
    }
    
    return C_TRUE;
}

void OptimFROG_freeTags(OptimFROG_Tags* tags) {
    if (!tags) return;
    for (uInt32_t i = 0; i < tags->keyCount; ++i) {
        free(tags->keys[i]);
        tags->keys[i] = nullptr;
        free(tags->values[i]);
        tags->values[i] = nullptr;
    }
    tags->keyCount = 0;
}

sInt32_t OptimFROG_decodeFile(char* sourceFile, char* destinationFile, OptimFROG_callBack callBack, void* callBackParam) {
    void* decoderInstance = OptimFROG_createInstance();
    if (!decoderInstance) return OptimFROG_MemoryError;
    
    void* buffer = malloc(0x2b110);
    if (!buffer) {
        OptimFROG_destroyInstance(decoderInstance);
        return OptimFROG_MemoryError;
    }
    
    if (!OptimFROG_open(decoderInstance, sourceFile, C_FALSE)) {
        OptimFROG_destroyInstance(decoderInstance);
        free(buffer);
        return OptimFROG_OpenError;
    }
    
    OptimFROG_Info info;
    OptimFROG_getInfo(decoderInstance, &info);
    
    FILE* outFile = fopen(destinationFile, "wb");
    if (!outFile) {
        OptimFROG_close(decoderInstance);
        OptimFROG_destroyInstance(decoderInstance);
        free(buffer);
        return OptimFROG_WriteError;
    }
    
    uInt32_t headSize = OptimFROG_readHead(decoderInstance, buffer, 0x2b110);
    if (fwrite(buffer, 1, headSize, outFile) != headSize) {
        OptimFROG_close(decoderInstance);
        OptimFROG_destroyInstance(decoderInstance);
        free(buffer);
        fclose(outFile);
        return OptimFROG_WriteError;
    }
    
    sInt64_t points_read = 0;
    uInt32_t bytes_per_point = (info.bitspersample / 8) * info.channels;
    uInt32_t points_per_chunk = 0x2b110 / bytes_per_point;
    
    while (points_read < info.noPoints) {
        if (callBack) {
            callBack(callBackParam, (double)points_read * 100.0 / (double)info.noPoints);
        }
        
        sInt32_t read_points = OptimFROG_read(decoderInstance, buffer, points_per_chunk, C_FALSE);
        if (read_points < 1) goto L_FATAL;
        
        size_t bytes_to_write = read_points * bytes_per_point;
        if (fwrite(buffer, 1, bytes_to_write, outFile) != bytes_to_write) {
            OptimFROG_close(decoderInstance);
            OptimFROG_destroyInstance(decoderInstance);
            free(buffer);
            fclose(outFile);
            return OptimFROG_WriteError;
        }
        points_read += read_points;
    }
    
    {
        sInt32_t tailSize = OptimFROG_readTail(decoderInstance, buffer, 0x2b110);
        if (tailSize < 0) goto L_FATAL;
        
        if (fwrite(buffer, 1, tailSize, outFile) != (size_t)tailSize) {
            OptimFROG_close(decoderInstance);
            OptimFROG_destroyInstance(decoderInstance);
            free(buffer);
            fclose(outFile);
            return OptimFROG_WriteError;
        }
        
        condition_t has_err = OptimFROG_recoverableErrors(decoderInstance);
        OptimFROG_close(decoderInstance);
        OptimFROG_destroyInstance(decoderInstance);
        free(buffer);
        if (fclose(outFile) != 0) return OptimFROG_WriteError;
        
        return has_err ? OptimFROG_RecoverableError : OptimFROG_NoError;
    }

L_FATAL:
    OptimFROG_close(decoderInstance);
    OptimFROG_destroyInstance(decoderInstance);
    free(buffer);
    fclose(outFile);
    return OptimFROG_FatalError;
}

sInt32_t OptimFROG_infoFile(char* sourceFile, OptimFROG_Info* info, OptimFROG_Tags* tags) {
    void* decoderInstance = OptimFROG_createInstance();
    if (!decoderInstance) return OptimFROG_MemoryError;
    
    if (OptimFROG_open(decoderInstance, sourceFile, tags != nullptr)) {
        OptimFROG_getInfo(decoderInstance, info);
        if (tags) {
            OptimFROG_getTags(decoderInstance, tags);
        }
        OptimFROG_close(decoderInstance);
        OptimFROG_destroyInstance(decoderInstance);
        return OptimFROG_NoError;
    }
    
    OptimFROG_destroyInstance(decoderInstance);
    return OptimFROG_OpenError;
}

} // extern "C"
