#ifndef PTR2_INT_H
#define PTR2_INT_H
#include "types.h"
#include <cstdio>
#include <cstring>

#define INTHDR_MAGIC 0x44332211

#define SPAHDR_MAGIC 0x59238771
#define SPAHDR_STRPS 0x18

#define SPMHDR_MAGIC 0x18DF540A
#define SPMHDR_STRPS 0x10

#define SPCHDR_MAGIC 0x09463AD8
#define SPCHDR_STRPS 0x18

#define INT_RESOURCE_END 0
#define INT_RESOURCE_TM0 1
#define INT_RESOURCE_SOUNDS 2
#define INT_RESOURCE_STAGE 3
#define INT_RESOURCE_HATCOLORBASE 4
#define HATCOLOR_RED 0
#define HATCOLOR_BLUE 1
#define HATCOLOR_PINK 2
#define HATCOLOR_YELLOW 3
#define INT_RESOURCE_TYPE_COUNT 8

namespace zng2xtr {
    extern const char *typenames[INT_RESOURCE_TYPE_COUNT];

    // Headers on .XTR
    struct header_t {
        u32 align;
        u32 dummy;
        u32 dummy2;
        u16 unkSectionSize;
        u16 ptrSectionQuty;
    };

    struct lzss_ptr_t {
	    u32 sectionEnd;
	    u32 lzssPointer;
	    u32 size;
	    u32 zero;
    };

    struct lzss_header_t {
        u32 uncompressed_size;
        u32 compressed_size;
        byte data[0];
    };

    typedef header_t *header_pt;
    typedef lzss_ptr_t *lzss_ptr_pt;
    typedef lzss_header_t *lzss_header_pt;

    inline bool checkinstall() {
        return (sizeof(header_t) == 0x20) || (sizeof(lzss_header_t) == 0x8);
    }
    header_t *getheader(const void *mint);
    lzss_ptr_t *getptrheader(const void *mint);
    u32 *getfiledataoffsets(const void *mint);
    /*filename_entry_t *getfilenameentries(const void *mint);*/
    lzss_header_t *getlzssheader(const void *mint, int lzssPointer);
    char *getfilenames(const void *mint);
    /*header_t *getnextheader(const void *mint);*/

    inline header_t *getheader(const void *mint) { return header_pt(mint); }
    inline lzss_ptr_t *getptrheader(const void *mint, int index) { return lzss_ptr_pt(mint + sizeof(header_t) + (sizeof(lzss_ptr_t) * index)); }

    inline u32 *getfiledataoffsets(const void *mint) { // TODO
        header_t *hdr = getheader(mint);
        return (u32 *)(hdr + 1);
    }

    inline lzss_header_t *getlzssheader(const void *mint, int lzssPointer) {
        return lzss_header_pt(mint + lzssPointer);
    }

    inline char *getfilenames(const void *mint) {
        const uint32_t* magic = reinterpret_cast<const uint32_t*>(mint);
        char fileExtension[5] = {};
        int pos = 0;

        switch(*magic) {
        case SPAHDR_MAGIC:
            pos = SPAHDR_STRPS;
            strncpy(fileExtension, ".spa", sizeof(fileExtension) - 1);
            break;
        case SPCHDR_MAGIC:
            pos = SPCHDR_STRPS;
            strncpy(fileExtension, ".spc", sizeof(fileExtension) - 1);
            break;
        case SPMHDR_MAGIC:
            pos = SPMHDR_STRPS;
            break;
        default: return nullptr;
        }

        const char *orgName = (char*)mint + pos;
        if(fileExtension[0] != '\0') {
            const size_t orgNameLen = strlen(orgName);
            const size_t fileExtLen = strlen(fileExtension);
            char* fileName = new char[orgNameLen + fileExtLen + 1];
            snprintf(fileName, orgNameLen + fileExtLen + 1, "%s%s", orgName, fileExtension);
            return fileName;
        } else {
            char *fileName = new char[strlen(orgName) + 1];
            strcpy(fileName, orgName);
            return fileName;
        }
    }

    // LZSS Sector Header

    struct comp_header_t {
        u32 dummy;
        u32 dummy2;
        u32 dummy3;
        u32 infoOff; // list of jumptables
        u32 infoSize; // size of the jumptable in bytes
        u32 dataOff;
        u32 dataSize;
        u32 fileCount;
    };

    struct comp_ptr_t {
        u32 offset;
        u32 zero;
        u32 dummy;
        u32 dummy2;
    };

    typedef comp_header_t *comp_header_pt;
    typedef comp_ptr_t *comp_ptr_pt;

    comp_header_t *getcompheader(const void *mint);
    comp_ptr_t *getcompptr(const void *mint, int ptrOffset, int index);

    inline comp_header_t *getcompheader(const void *mint) { return comp_header_pt(mint); }
    inline comp_ptr_t *getcompptr(const void *mint, int ptrOffset, int index) { return comp_ptr_pt(mint + ptrOffset + (sizeof(comp_ptr_t) * index)); }
}

#endif
