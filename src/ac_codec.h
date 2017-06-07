#ifndef AC_CODEC
#define AC_CODEC

#include <stdio.h>

/// Static model for general data.
class StaticDataModel
{
    public:
        StaticDataModel(void);
        ~StaticDataModel(void);
        unsigned modelSymbols(void) { return dataSymbols; }
        void setDistribution(unsigned numberOfSymbols, const double probability[] = 0); /// 0 means uniform

    private:
        unsigned * distribution, * decoderTable;
        unsigned dataSymbols, lastSymbol, tableSize, tableShift;
        friend class ArithmeticCodec;
};

/// Adaptive model for binary data.
class AdaptiveDataModel
{
    public:
        AdaptiveDataModel(void);
        AdaptiveDataModel(unsigned numberOfSymbols);
        ~AdaptiveDataModel(void);
        unsigned modelSymbols(void) { return dataSymbols; }
        void reset(void); /// Reset to equiprobable model.
        void setAlphabet(unsigned numberOfSymbols);

    private:
        void update(bool);
        unsigned * distribution, * symbolCount, * decoderTable;
        unsigned totalCount, updateCycle, symbolsUntilUpdate;
        unsigned dataSymbols, lastSymbol, tableSize, tableShift;
        friend class ArithmeticCodec;
};

/// Class with both the arithmetic encoder and decoder.
/// All compressed data is saved to a memory buffer.
class ArithmeticCodec
{
    public:
        ArithmeticCodec(void);
        ~ArithmeticCodec(void);
        ArithmeticCodec(unsigned maxEncodedBytes, unsigned char * userBuffer = 0); /// 0 -> assign new

        unsigned char * buffer(void) { return codeBuffer; }
        void     setBuffer(unsigned maxEncodedBytes, unsigned char * userBuffer = 0); /// 0 -> assign new

        void     startEncoder(void);
        void     startDecoder(void);

        void     readFromFile(FILE * encodedFile); /// Read encoded data and then start decoder.
        unsigned readFromInputBuffer(unsigned char* buffer, int offset);
        unsigned stopEncoder(void); /// Returns number of bytes used.

        unsigned writeToFile(FILE * encodedFile); /// Stop encoder and then write encoded data.
        unsigned writeToOutputBuffer(unsigned char* buffer, int offset);
        void     stopDecoder(void);

        void     encode(unsigned data, StaticDataModel &);
        unsigned decode(StaticDataModel &);

        void     encode(unsigned data, AdaptiveDataModel &);
        unsigned decode(AdaptiveDataModel &);

    private:
        void propagateCarry(void);
        void renormEncryptionInterval(void);
        void renormDecryptionInterval(void);
        unsigned char * codeBuffer, * newBuffer, * acPointer;
        unsigned base, value, length; /// Arithmetic coding state.
        unsigned bufferSize, mode; /// Mode: 0 = undefined, 1 = encoder, 2 = decoder.
};

#endif
