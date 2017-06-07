#include <stdlib.h>
#include <chrono>

#include "ac_codec.h"

const char * WRITE_ERROR_MSG = "cannot write to file";
const char * READ_ERROR_MSG = "cannot read from file";

const unsigned numModels  = 16; /// MUST be a power of 2
const unsigned bufferSize = 65536;
const unsigned FILE_ID    = 0xA8BC3B39U;

void encodeFile(char * dataFileName, char * encodedFileName);
void decodeFile(char * encodedFileName, char * dataFileName);

int main(int numberOfArguments, char * arguments[])
{
    auto start = std::chrono::system_clock::now();
    if ((numberOfArguments != 4) || (arguments[1][0] != '-') || ((arguments[1][1] != 'c') && (arguments[1][1] != 'd')))
    {
        puts("\n Compression parameters:   ArithmeticCodeCodec -c data_file_name compressed_file_name");
        puts("\n Decompression parameters: ArithmeticCodeCodec -d compressed_file_name new_file_name\n");
        exit(0);
    }

    if (arguments[1][1] == 'c') encodeFile(arguments[2], arguments[3]);
    else decodeFile(arguments[2], arguments[3]);

    auto end = std::chrono::system_clock::now();
    std::chrono::duration<double> diff = end-start;
    printf(" Execution time: %.3f ms", diff.count() * 1000);
    return 0;
}

void printError(const char * s)
{
    fprintf(stderr, "\n Error: %s.\n\n", s);
    exit(1);
}

unsigned bufferCRC(unsigned bytes, unsigned char * buffer)
{
    static const unsigned CRC_Generation_Data[8] = /// Data needed for generating CRC table.
    {
        0xEC1A5A3EU, 0x5975F5D7U, 0xB2EBEBAEU, 0xE49696F7U,
        0x486C6C45U, 0x90D8D88AU, 0xA0F0F0BFU, 0xC0A0A0D5U
    };

    static unsigned CRC_Table[256];

    if (CRC_Table[1] == 0) /// Compute table.
        for (unsigned k = CRC_Table[0] = 0; k < 8; k++)
        {
            unsigned s = 1 << k, g = CRC_Generation_Data[k];
            for (unsigned n = 0; n < s; n++)
                CRC_Table[n+s] = CRC_Table[n] ^ g;
        }

    /// Compute buffer's cyclic redundancy check.
    unsigned crc = 0;
    if (bytes)
        do
        {
            crc = (crc >> 8) ^ CRC_Table[(crc&0xFFU) ^ (unsigned)(*buffer++)];
        }
        while (--bytes);
    return crc;
}

FILE * openInputFile(char * fileName)
{
    FILE * newFile = fopen(fileName, "rb");
    if (newFile == NULL) printError("cannot open input file");
    return newFile;
}

FILE * openOutputFile(char * fileName)
{
    FILE * newFile = fopen(fileName, "rb");
    if (newFile != NULL)
    {
        fclose(newFile);
        printf("\n Overwrite file %s? (y = yes, else quit) ", fileName);
        char input[256];
        gets(input);
        if (input[0] != 'y') exit(0);
    }
    newFile = fopen(fileName, "wb");
    if (newFile == NULL) printError("cannot open output file");
    return newFile;
}

void saveNumber(unsigned number, unsigned char * buff) /// Decompose 4-byte number and write it to buffer.
{
    buff[0] = (unsigned char)( number        & 0xFFU);
    buff[1] = (unsigned char)((number >>  8) & 0xFFU);
    buff[2] = (unsigned char)((number >> 16) & 0xFFU);
    buff[3] = (unsigned char)( number >> 24         );
}

unsigned recoverSavedNumber(unsigned char * buff) /// Recover 4-byte integer from buffer.
{
    return unsigned(buff[0]) + (unsigned(buff[1]) << 8) + (unsigned(buff[2]) << 16) + (unsigned(buff[3]) << 24);
}

void encodeFile(char * dataFileName, char * encodedFileName)
{
    FILE * dataFile = openInputFile(dataFileName);
    FILE * encodedFile = openOutputFile(encodedFileName);

    unsigned char * data = new unsigned char[bufferSize]; /// Buffer for input file data.

    unsigned nb, bytes = 0, crc = 0; /// Compute CRC (cyclic redundancy check) of file.
    do
    {
        nb = fread(data, 1, bufferSize, dataFile);
        bytes += nb;
        crc ^= bufferCRC(nb, data);
    }
    while (nb == bufferSize);

    /// define 12-byte header
    unsigned char header[12];
    saveNumber(FILE_ID, header    );
    saveNumber(crc,     header + 4);
    saveNumber(bytes,   header + 8);
    if (fwrite(header, 1, 12, encodedFile) != 12) printError(WRITE_ERROR_MSG);
    AdaptiveDataModel dataModel[numModels]; /// Set data models.
    for (unsigned m = 0; m < numModels; m++) dataModel[m].setAlphabet(256);

    ArithmeticCodec encoder(bufferSize); /// Set encoder buffer.

    rewind(dataFile); /// So the file can be read again.

    unsigned context = 0;
    do
    {
        nb = (bytes < bufferSize ? bytes : bufferSize);
        if (fread(data, 1, nb, dataFile) != nb) printError(READ_ERROR_MSG); /// Read input file data.

        encoder.startEncoder();
        for (unsigned p = 0; p < nb; p++) /// Compress data.
        {
            encoder.encode(data[p], dataModel[context]);
            context = (unsigned)(data[p]) & (numModels - 1);
        }

        encoder.writeToFile(encodedFile);  /// Stop the encoder and write compressed data.
    }
    while (bytes -= nb);

    /// Clean up code.
    fflush(encodedFile);
    unsigned dataBytes = ftell(dataFile), encodedBytes = ftell(encodedFile);
    printf(" Compressed file size = %d bytes (%.3f:1 compression)\n", encodedBytes, double(dataBytes) / double(encodedBytes));
    fclose(dataFile);
    fclose(encodedFile);

    delete [] data;
}

void decodeFile(char * encodedFileName, char * dataFileName)
{
    FILE * encodedFile = openInputFile(encodedFileName);
    FILE * dataFile = openOutputFile(dataFileName);

    /// Read file information from 12-byte header.
    unsigned char header[12];
    if (fread(header, 1, 12, encodedFile) != 12) printError(READ_ERROR_MSG);
    unsigned fileID = recoverSavedNumber(header);
    unsigned crc    = recoverSavedNumber(header + 4);
    unsigned bytes  = recoverSavedNumber(header + 8);

    if (fileID != FILE_ID) printError("invalid compressed file");

    unsigned char * data = new unsigned char[bufferSize]; /// Buffer for output file data.

    /// Set data models.
    AdaptiveDataModel dataModel[numModels];
    for (unsigned m = 0; m < numModels; m++) dataModel[m].setAlphabet(256);

    ArithmeticCodec decoder(bufferSize); /// Set encoder buffer.

    /// Decompress file.
    unsigned nb, newCRC = 0, context = 0;
    do
    {
        decoder.readFromFile(encodedFile); /// Read compressed data and start decoder.

        nb = (bytes < bufferSize ? bytes : bufferSize);
        /// Decompress data.
        for (unsigned p = 0; p < nb; p++)
        {
            data[p] = (unsigned char) decoder.decode(dataModel[context]);
            context = (unsigned)(data[p]) & (numModels - 1);
        }
        decoder.stopDecoder();

        newCRC ^= bufferCRC(nb, data); /// Compute CRC of the new file.
        if (fwrite(data, 1, nb, dataFile) != nb) printError(WRITE_ERROR_MSG);

    }
    while (bytes -= nb);

    fclose(dataFile);
    fclose(encodedFile);

    delete [] data;
    /// Check file validity.
    if (crc != newCRC) printError("incorrect file CRC");
}
