#include <dirent.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <string>
#include <vector>

#include "xtr.h"
#include "types.h"
extern "C" {
    #include "lzss.h"
}
#include "zng2cmd.h"
#include "zng2common.h"

#ifdef _WIN32
    #include <direct.h>
    #define mkdir(x, y) _mkdir(x)
#endif

#define INFO(s, ...) printf(s, __VA_ARGS__);
#define ERROR(s, ...) fprintf(stderr, s, __VA_ARGS__);
#define FATAL(ec, s, ...)          \
  fprintf(stderr, s, __VA_ARGS__); \
  exit(ec);

static int cmd_extract(int argc, char *args[]);

cmd_t commands[] = {
    {
        "extract",
        "[xtrfile] [folder]",
        "Extract contents of an XTR file to a folder.",
        {
            "e",
            "ex",
            "x"
        },
        3,
        cmd_extract
    }
};

bool direxists(const char *dirname) {
    struct stat st;
    if (stat(dirname, &st) != 0)
      return false;

    if (S_ISDIR(st.st_mode))
      return true;
    else
        return false;
}

bool makedir(const char *newdir) {
    struct stat st;
    if (direxists(newdir)) return true;
    int err = stat(newdir, &st);
    if (err == -1) {
        if (mkdir(newdir, S_IRWXU) != 0) {
            ERROR("Error making dir %s\n", newdir);
            return false;
        }
    } else if (0 == err) {
        if (S_ISDIR(st.st_mode))
            return true;
        else
            return false;
    }
    return true;
}

struct intfile_t {
    intfile_t(std::string fn, int filesize, int fdp);
    std::string filename;
    int filesize;
    int offset;
};

intfile_t::intfile_t(std::string fn, int filesize, int fdp): filename(fn), filesize(filesize), offset(fdp) {

}

#define ALIGN(x, y) (((x) + (y - 1)) & (~((y)-1)))

bool isfile(const char *fn) {
    struct stat st;
    if (stat(fn, &st) != 0)
        return false;

    if (S_ISREG(st.st_mode))
        return true;
    else
        return false;
}

struct resfile_iterator_t {
    virtual ~resfile_iterator_t() {

    };
    virtual const char *next() = 0;
};

struct resfile_dir_iterator_t: resfile_iterator_t {
    DIR *dir;
    struct dirent *de;
    resfile_dir_iterator_t(DIR *_dir) : dir(_dir){};
    ~resfile_dir_iterator_t() { closedir(dir); }
    const char *next();
};

const char *resfile_dir_iterator_t::next() {
    de = readdir(dir);
    if (de == NULL) return NULL;
    return de->d_name;
}

struct resfile_order_iterator_t: resfile_iterator_t {
    FILE *orderfile;
    char buf[256];
    resfile_order_iterator_t(FILE *f): orderfile(f) {

    }
    ~resfile_order_iterator_t() {
        this->close();
    }
    const char *next();
    void close() {
        if (NULL != orderfile) {
            fclose(orderfile);
            orderfile = NULL;
        }
        return;
    }
};

const char *resfile_order_iterator_t::next() {
    buf[0] = char(0);
    while (!feof(orderfile)) {
        fscanf(orderfile, "%512s\n", buf);
        if (buf[0] == char(0))
            continue;
        return buf;
    }
    return NULL;
}

resfile_iterator_t *openiterator(const char *dirname) {
    char lbuf[256];
    snprintf(lbuf, sizeof(lbuf), "%s/_order.txt", dirname);
    if (isfile(lbuf)) {
      FILE *orderfile = fopen(lbuf, "r");
      if (NULL != orderfile)
        return new resfile_order_iterator_t(orderfile);
    }
    DIR *dir = opendir(dirname);
    if (NULL == dir)
      return NULL;

    return new resfile_dir_iterator_t(dir);
}

int pad_folderdata(byte *folderdata, int start, int end) {
    int remain = end - start;
    while (remain > 0) {
        // TODO: Repeating last few bytes of file might help compression
        //       instead of zero-filling. The official INT packer
        //       zero-fills.
        folderdata[end - remain] = 0;
        remain--;
    }
    return (end - start);
}

#define REQUIRE(x, c)              \
    if (argc < x) {                \
        commands[c].printhelp(""); \
        return 1;                  \
    }

static void extractTarball(const char *dirname, const char *restypename, byte *uncompressed, int size) {
    int unkCount = 0;
    char lbuf[256];

    zng2xtr::comp_header_t *comp_hdr = zng2xtr::getcompheader(uncompressed);

    printf("Extracting %d files (TYPE: %s)\n", comp_hdr->fileCount, restypename);

    // Create XTR Section Folder
    snprintf(lbuf, sizeof(lbuf), "%s/%s", dirname, restypename);
    makedir(lbuf);

    // (DEBUG) Extract tarball to a file
    printf("(DEBUG) Extracting raw data...\n");
    snprintf(lbuf, sizeof(lbuf), "%s/%s/_rawsection", dirname, restypename);
    FILE *rawfile = fopen(lbuf, "w");
    fwrite(uncompressed, 1, size, rawfile);
    fclose(rawfile);

    // Open order file
    snprintf(lbuf, sizeof(lbuf), "%s/%s/_order.txt", dirname, restypename);
    FILE *orderfile = fopen(lbuf, "w");

    // File extraction loop
    zng2xtr::comp_ptr_t *nextFileHeader;
    int fileCount = comp_hdr->infoSize / 16;
    for (u32 i = 0, skip = 0; i < fileCount; i++) {
        // Get File Info
        zng2xtr::comp_ptr_t *fileHeader = zng2xtr::getcompptr(uncompressed, comp_hdr->infoOff, i);
        if(fileHeader->offset == 0x00) {fprintf(orderfile, "skip\n"); skip++; continue;} // Skip files with zero offset

        bool isLastFile = i == fileCount - 1;
        int compIncrease = 0;
        do { // Skip past zeroed offsets
            compIncrease++;
            nextFileHeader = zng2xtr::getcompptr(uncompressed, comp_hdr->infoOff, i + compIncrease);
        } while(nextFileHeader->offset == 0x00);
        int fileSize = (isLastFile ? size : nextFileHeader->offset) - fileHeader->offset;

        char *fileName = zng2xtr::getfilenames(uncompressed + fileHeader->offset);
        if(fileName == nullptr) {
            fileName = new char[6];
            sprintf(fileName, "%0*d", 5, i); // Generate file name
            unkCount++;
        }

        snprintf(lbuf, sizeof(lbuf), "%s/%s/%s", dirname, restypename, fileName);
        FILE *outfile = fopen(lbuf, "wb");
        if (NULL == outfile) { INFO("Couldn't open %s...\n", lbuf); continue; }

        printf("WRITE: %s (%d bytes)\n", fileName, fileSize);
        fwrite(uncompressed + fileHeader->offset, 1, fileSize, outfile);

        fclose(outfile);
        fprintf(orderfile, "%s\n", fileName);
    }
}

static int cmd_extract(int argc, char *args[]) {
    REQUIRE(2, 1);                                            // make sure we're running the right command
    const char *xtrfilename = args[0];                        // get filename
    const char *outputfilename = args[1];                     // get output folder name
    FILE *f = fopen(xtrfilename, "rb");                       // open file
    int len = getfilesize(f);                                 // get size
    void *mint = malloc(len);                                 // allocate file in memory
    fread(mint, 1, len, f);                                   // put file in there
    char lbuf[256];                                           // establish char buffer


    //assertheader(mint, "Not an XTR file %s\n", intfilename);  // TODO: Checking for XTR, use some data as check

    INFO("INFLATE %s\n", xtrfilename);
    zng2xtr::header_t *hdr = zng2xtr::getheader(mint);
    byte *history = (byte *)(malloc(4096)); // allow 4096 bytes for history
    int lastLzssPos = 0;
    int count = 0;

    // Create XTR Output Folder
    makedir(outputfilename);

    // LZSS Section
    while(true) {
        zng2xtr::lzss_ptr_t *ptr = zng2xtr::getptrheader(hdr, count);
        if(ptr->lzssPointer == 0) break;

        zng2xtr::lzss_header_t *lzss = zng2xtr::getlzssheader(hdr, ptr->lzssPointer);
        const char *restypename = zng2xtr::typenames[count];
        lastLzssPos = ptr->lzssPointer + ptr->size;//+ lzss->compressed_size;
        printf("INFLATE: Section %s\n", restypename);

        // Set space for compressed and uncompressed data
        byte *uncompressed = (byte *)(malloc(lzss->uncompressed_size));
        byte *compressed = lzss->data;

        // Decompress
        memset(history, 0, 4096);
        lzss_decompress(12, 4, 2, 2, history, compressed, lzss->compressed_size, uncompressed, lzss->uncompressed_size);

        extractTarball(outputfilename, restypename, uncompressed, lzss->uncompressed_size);

        count++;
        free(uncompressed);
    }

    // If that was bad, this will be even worse

    int pos = lastLzssPos;

    int chunkSize = 512;
    int dataSize = 512 - 16; // Used on extraction
    int audioSize = 1024;
    int preSize = 0;
    int chnSize = 0;

    snprintf(lbuf, sizeof(lbuf), "%s/_fileinfo", outputfilename);
    FILE *fileinfo = fopen(lbuf, "wb");

    // Skip until there's audio, get alignment
    uint8_t *currentChunk = ((uint8_t*)((char*)mint + pos));
    while(memcmp(currentChunk, "\x00\x00\x00\x00\x00\x00\x00\x00", 8) == 0) {
        pos += chunkSize;
        currentChunk = ((uint8_t*)((char*)mint + pos));
    }
    preSize = pos - lastLzssPos;
    pos += audioSize;

    // Read space between audios
    currentChunk = ((uint8_t*)((char*)mint + pos));
    while(memcmp(currentChunk, "\x00\x00\x00\x00\x00\x00\x00\x00", 8) == 0) {
        pos += chunkSize;
        chnSize += chunkSize;
        currentChunk = ((uint8_t*)((char*)mint + pos));
    }

    // Print config to a file
    fprintf(fileinfo, "Interlace offset: %d \n", preSize);
    fprintf(fileinfo, "Uncompressed data size: %d \n", chnSize);
    fclose(fileinfo); // TODO: Needs more info for creating XTR again

    // Audio section
    printf("Extracting audio file\n");

    char fileName[255];
    sprintf(fileName, "audio.wp2"); // Generate file name

    snprintf(lbuf, sizeof(lbuf), "%s/%s", outputfilename, fileName);
    FILE *audfile = fopen(lbuf, "wb");

    pos = lastLzssPos + preSize;
    int binSize = 0;
    while(pos < len) {
        fwrite(mint + pos, 1, audioSize, audfile);
        pos += audioSize + chnSize;
        binSize += audioSize; // just for logging!
    }
    printf("WRITE: %s (%d bytes)\n", fileName, binSize);
    fclose(audfile);

    // Unknown ammount of uncompressed files section
    printf("Extracting uncompressed files\n");

    int uBatchCount = chnSize / chunkSize;
    pos = lastLzssPos + preSize + audioSize;

    int fileCount = 0;
    int truncate = 0;
    int tmpPos = 0;
    int tmpTrn = 0;
    int uCount = 0;
    int blankCount = 0;
    int allocSize = 0;
    bool first = false;
    while(pos + chunkSize < len) { // TODO: Stinky ass while loop
        pos += chunkSize;
        truncate++;

        if(truncate == uBatchCount) { pos = ALIGN(pos + audioSize, chunkSize); truncate = 0; } // Skip Audio Data

        // Get position data for check
        uint8_t *checkChunk = ((uint8_t*)((char*)mint + pos));
        if(checkChunk[0] != 00 && checkChunk[1] != 00) {
            // Counting size

            if(!first) { tmpPos = pos; tmpTrn = truncate; first = true; } // Starting position for the file
            allocSize += chunkSize - 16;
            uCount++;
        } else if(first) {
            // Count end -- extract!
            byte *uncompressed = (byte *)(malloc(allocSize));

            // Copy chunks into allocated space
            for(int i = 0; i < uCount; i++) {
                memcpy(uncompressed + (i * dataSize), mint + tmpPos + 16, dataSize);
                tmpPos += chunkSize;
                tmpTrn++;

                if(tmpTrn == uBatchCount) { tmpPos = ALIGN(tmpPos + audioSize, chunkSize);  tmpTrn = 0; } // Skip Audio Data
            }

            // Generate folder name
            fileCount++;
            const char *folderName;
            char folderNameBuffer[255];
            sprintf(folderNameBuffer, "UNC_%d", fileCount); // Generate file name
            folderName = folderNameBuffer;

            // Extract
            extractTarball(outputfilename, folderName, uncompressed, allocSize);

            // Reset everything for another go
            free(uncompressed);
            first = false;
            allocSize = 0;
            uCount = 0;
        } else {
            blankCount++; // Increase blank chunks for use in creating
        }
    }

    free(history);
    printf("Done.\n");
    return 0;
}

int main(int argc, char *args[]) {
    if (zng2xtr::checkinstall() == false) {
        // if it didnt compile well, will barely show
        fprintf(stderr, "Bad compile\n");
        return 2;
    }

    if (argc <= 1) {
        // if no arguments
        for (cmd_t &cmd : commands) {
            cmd.printhelp("");
        }
        return 1;
    } else {
        for (cmd_t &cmd : commands) {    // find matching command
            if (cmd.matches(args[1])) {  // epic GAMER STYLE we found a match
                return cmd.exec(argc - 2, args + 2);
            }
        }
        printf("zng2xtr:   Unknown command %s\n", args[1]);
        return 1;
    }
}

// Se você estiver lendo isso, não me culpe pela bagunça.
