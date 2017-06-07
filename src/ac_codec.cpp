#include <stdlib.h>
#include <memory.h>
#include "ac_codec.h"

const unsigned AC__MinLength = 0x01000000U;   /// Threshold for renormalization.
const unsigned AC__MaxLength = 0xFFFFFFFFU;   /// Maximum arithmetic coding interval length.

/// Maximum values for general models
const unsigned DM__LengthShift = 15; /// Length of bits discarded before mult.
const unsigned DM__MaxCount    = 1 << DM__LengthShift; /// For adaptive models.

static void AC_Error(const char * msg)
{
    fprintf(stderr, "\n\n -> Arithmetic coding error: ");
    fputs(msg, stderr);
    fputs("\n Execution terminated!\n", stderr);
    exit(1);
}

/// Carry propagation on compressed data buffer.
inline void ArithmeticCodec::propagateCarry()
{
    unsigned char * p;
    for (p = acPointer - 1; *p == 0xFFU; p--) *p = 0;
    ++*p;
}

inline void ArithmeticCodec::renormEncryptionInterval()
{
    /// Output and discard top byte.
    do
    {
        *acPointer++ = (unsigned char)(base >> 24);
        base <<= 8;
    }
    while ((length <<= 8) < AC__MinLength); /// Length multiplied by 256.
}

inline void ArithmeticCodec::renormDecryptionInterval()
{
    /// Read least-significant byte.
    do
    {
        value = (value << 8) | unsigned(*++acPointer);
    }
    while ((length <<= 8) < AC__MinLength); /// Length multiplied by 256.
}

void ArithmeticCodec::encode(unsigned data, StaticDataModel & model)
{
    unsigned x, initialBase = base;

    /// compute products
    if (data == model.lastSymbol)
    {
        x = model.distribution[data] * (length >> DM__LengthShift);
        base   += x; /// Update interval.
        length -= x; /// No product needed.
    }
    else
    {
        x = model.distribution[data] * (length >>= DM__LengthShift);
        base += x; /// Update interval.
        length = model.distribution[data+1] * length - x;
    }

    if (initialBase > base) propagateCarry(); /// overflow = carry

    if (length < AC__MinLength) renormEncryptionInterval(); /// Renormalization.
}

unsigned ArithmeticCodec::decode(StaticDataModel & model)
{
    unsigned n, s, x, y = length;

    if (model.decoderTable)
    {
        /// Use table look-up for faster decoding.
        unsigned dv = value / (length >>= DM__LengthShift);
        unsigned t = dv >> model.tableShift;

        /// Initial decision based on table look-up.
        s = model.decoderTable[t];
        n = model.decoderTable[t+1] + 1;

        while (n > s + 1)
        {
            /// Finish with bisection search.
            unsigned m = (s + n) >> 1;
            if (model.distribution[m] > dv) n = m;
            else s = m;
        }
        /// Compute products.
        x = model.distribution[s] * length;
        if (s != model.lastSymbol) y = model.distribution[s+1] * length;
    }
    else
    {
        /// Decode using only multiplications.
        x = s = 0;
        length >>= DM__LengthShift;
        unsigned m = (n = model.dataSymbols) >> 1;
        /// Decode via bisection search.
        do
        {
            unsigned z = length * model.distribution[m];
            if (z > value)
            {
                n = m;
                y = z;
            }
            else
            {
                s = m;
                x = z;
            }
        }
        while ((m = (s + n) >> 1) != s);
    }

    /// Update interval.
    value -= x;
    length = y - x;

    if (length < AC__MinLength) renormDecryptionInterval(); /// Renormalization.

    return s;
}

void ArithmeticCodec::encode(unsigned data, AdaptiveDataModel & model)
{
    unsigned x, initialBase = base;
    /// Compute products.
    if (data == model.lastSymbol)
    {
        x = model.distribution[data] * (length >> DM__LengthShift);
        base   += x; /// Update interval.
        length -= x; /// No product needed.
    }
    else
    {
        x = model.distribution[data] * (length >>= DM__LengthShift);
        base   += x; /// Update interval.
        length  = model.distribution[data+1] * length - x;
    }

    if (initialBase > base) propagateCarry(); /// overflow = carry

    if (length < AC__MinLength) renormEncryptionInterval(); /// Renormalization.

    ++model.symbolCount[data];
    if (--model.symbolsUntilUpdate == 0) model.update(true);  /// Periodic model update.
}

unsigned ArithmeticCodec::decode(AdaptiveDataModel & model)
{
    unsigned n, s, x, y = length;

    if (model.decoderTable)
    {
        /// Use table look-up for faster decoding.
        unsigned dv = value / (length >>= DM__LengthShift);
        unsigned t = dv >> model.tableShift;

        /// Initial decision based on table look-up.
        s = model.decoderTable[t];
        n = model.decoderTable[t+1] + 1;

        /// Finish with bisection search.
        while (n > s + 1)
        {
            unsigned m = (s + n) >> 1;
            if (model.distribution[m] > dv) n = m;
            else s = m;
        }
        /// Compute products.
        x = model.distribution[s] * length;
        if (s != model.lastSymbol) y = model.distribution[s+1] * length;
    }
    else
    {
        /// Decode using only multiplications.
        x = s = 0;
        length >>= DM__LengthShift;
        unsigned m = (n = model.dataSymbols) >> 1;

        /// Decode via bisection search.
        do
        {
            unsigned z = length * model.distribution[m];
            if (z > value)
            {
                n = m;
                y = z;
            }
            else
            {
                s = m;
                x = z;
            }
        }
        while ((m = (s + n) >> 1) != s);
    }

    value -= x; /// Update interval
    length = y - x;

    if (length < AC__MinLength) renormDecryptionInterval(); /// Renormalization.

    ++model.symbolCount[s];
    if (--model.symbolsUntilUpdate == 0) model.update(false);  /// Periodic model update.

    return s;
}

ArithmeticCodec::ArithmeticCodec()
{
    mode = bufferSize = 0;
    newBuffer = codeBuffer = 0;
}

ArithmeticCodec::ArithmeticCodec(unsigned maxEncodedBytes, unsigned char * userBuffer)
{
    mode = bufferSize = 0;
    newBuffer = codeBuffer = 0;
    setBuffer(maxEncodedBytes, userBuffer);
}

ArithmeticCodec::~ArithmeticCodec()
{
    delete [] newBuffer;
}

void ArithmeticCodec::setBuffer(unsigned maxEncodedBytes, unsigned char * userBuffer)
{
    /// Test for reasonable sizes.
    if ((maxEncodedBytes < 16) || (maxEncodedBytes > 0x1000000U))
        AC_Error("invalid codec buffer size");
    if (mode != 0) AC_Error("cannot set buffer while encoding or decoding");

    /// User provides memory buffer.
    if (userBuffer != NULL)
    {
        bufferSize = maxEncodedBytes;
        codeBuffer = userBuffer; /// Set buffer for compressed data.
        delete [] newBuffer; /// Free anything previously assigned.
        newBuffer = 0;
        return;
    }

    if (maxEncodedBytes <= bufferSize) return; /// Enough available space in buffer

    bufferSize = maxEncodedBytes; /// Assign new memory.
    delete [] newBuffer; /// Free anything previously assigned.
    if ((newBuffer = new unsigned char[bufferSize+16]) == 0) /// 16 extra bytes
        AC_Error("cannot assign memory for compressed data buffer");
    codeBuffer = newBuffer; /// Set buffer for compressed data.
}

void ArithmeticCodec::startEncoder()
{
    if (mode != 0) AC_Error("cannot start encoder");
    if (bufferSize == 0) AC_Error("no code buffer set");

    mode   = 1;
    base   = 0;
    /// Initialize encoder variables: interval and pointer.
    length = AC__MaxLength;
    acPointer = codeBuffer; /// Pointer to next data byte.
}

void ArithmeticCodec::startDecoder()
{
    if (mode != 0) AC_Error("cannot start decoder");
    if (bufferSize == 0) AC_Error("no code buffer set");

    /// Initialize decoder: interval, pointer, initial code value.
    mode   = 2;
    length = AC__MaxLength;
    acPointer = codeBuffer + 3;
    value = ((unsigned)(codeBuffer[0]) << 24) | ((unsigned)(codeBuffer[1]) << 16) |
            ((unsigned)(codeBuffer[2]) <<  8) | (unsigned)(codeBuffer[3]);
}

unsigned ArithmeticCodec::readFromInputBuffer(unsigned char* buffer, int offset)
{
    unsigned shift = 0, codeBytes = 0;
    int fileByte;

    do
    {
        fileByte = buffer[offset];
        codeBytes |= unsigned(fileByte & 0x7F) << shift;
        shift += 7;
        offset++;
    }
    while (fileByte & 0x80);

    memcpy(codeBuffer, buffer + offset, codeBytes);
    offset += codeBytes;

    startDecoder();
    return offset;
}

void ArithmeticCodec::readFromFile(FILE * encodedFile)
{
    unsigned shift = 0, codeBytes = 0;
    int fileByte;

    /// Read variable-length header with number of code bytes.
    do
    {
        if ((fileByte = getc(encodedFile)) == EOF)
            AC_Error("cannot read code from file");
        codeBytes |= unsigned(fileByte & 0x7F) << shift;
        shift += 7;
    }
    while (fileByte & 0x80);
    /// Read compressed data.
    if (codeBytes > bufferSize) AC_Error("code buffer overflow");
    if (fread(codeBuffer, 1, codeBytes, encodedFile) != codeBytes)
        AC_Error("cannot read code from file");

    startDecoder(); /// Initialize decoder.
}

unsigned ArithmeticCodec::stopEncoder()
{
    if (mode != 1) AC_Error("invalid to stop encoder");
    mode = 0;

    /// Done encoding: set final data bytes
    unsigned initialBase = base;

    if (length > 2 * AC__MinLength)
    {
        base  += AC__MinLength; /// Base offset.
        length = AC__MinLength >> 1; /// Set new length for 1 more byte.
    }
    else
    {
        base  += AC__MinLength >> 1; /// Base offset.
        length = AC__MinLength >> 9; /// Set new length for 2 more bytes.
    }

    if (initialBase > base) propagateCarry(); /// overflow = carry

    renormEncryptionInterval(); /// Renormalization = output last bytes.

    unsigned codeBytes = unsigned(acPointer - codeBuffer);
    if (codeBytes > bufferSize) AC_Error("code buffer overflow");

    return codeBytes; /// Number of bytes used.
}

unsigned ArithmeticCodec::writeToOutputBuffer(unsigned char* buffer, int offset)
{
    unsigned headerBytes = 0, codeBytes = stopEncoder(), nb = codeBytes;

    /// Write variable-length header with number of code bytes.
    do
    {
        int fileByte = int(nb & 0x7FU);
        if ((nb >>= 7) > 0) fileByte |= 0x80;
        buffer[offset + headerBytes] = fileByte;
        headerBytes++;
    }
    while (nb);

    memcpy(buffer + offset + headerBytes, codeBuffer, codeBytes);
    return offset + codeBytes + headerBytes;  /// Bytes used.
}

unsigned ArithmeticCodec::writeToFile(FILE * encodedFile)
{
    unsigned headerBytes = 0, codeBytes = stopEncoder(), nb = codeBytes;

    /// Write variable-length header with number of code bytes.
    do
    {
        int fileByte = int(nb & 0x7FU);
        if ((nb >>= 7) > 0) fileByte |= 0x80;
        if (putc(fileByte, encodedFile) == EOF)
            AC_Error("cannot write compressed data to file");
        headerBytes++;
    }
    while (nb);

    /// Write compressed data.
    if (fwrite(codeBuffer, 1, codeBytes, encodedFile) != codeBytes)
        AC_Error("cannot write compressed data to file");

    return codeBytes + headerBytes; /// Bytes used.
}

void ArithmeticCodec::stopDecoder()
{
    if (mode != 2) AC_Error("invalid to stop decoder");
    mode = 0;
}

StaticDataModel::StaticDataModel()
{
    dataSymbols = 0;
    distribution = 0;
}

StaticDataModel::~StaticDataModel()
{
    delete [] distribution;
}

void StaticDataModel::setDistribution(unsigned numberOfSymbols, const double probability[])
{
    if ((numberOfSymbols < 2) || (numberOfSymbols > (1 << 11)))
        AC_Error("invalid number of data symbols");

    /// Assign memory for data model.
    if (dataSymbols != numberOfSymbols)
    {
        dataSymbols = numberOfSymbols;
        lastSymbol = dataSymbols - 1;
        delete [] distribution;
        /// Define size of table for fast decoding.
        if (dataSymbols > 16)
        {
            unsigned tableBits = 3;
            while (dataSymbols > (1U << (tableBits + 2))) ++tableBits;
            tableSize  = (1 << tableBits) + 4;
            tableShift = DM__LengthShift - tableBits;
            distribution = new unsigned[dataSymbols+tableSize+6];
            decoderTable = distribution + dataSymbols;
        }
        /// Small alphabet: no table needed.
        else
        {
            decoderTable = 0;
            tableSize = tableShift = 0;
            distribution = new unsigned[dataSymbols];
        }
        if (distribution == 0) AC_Error("cannot assign model memory");
    }
    /// Compute cumulative distribution, decoder table.
    unsigned s = 0;
    double sum = 0.0, p = 1.0 / double(dataSymbols);

    for (unsigned k = 0; k < dataSymbols; k++)
    {
        if (probability) p = probability[k];
        if ((p < 0.0001) || (p > 0.9999)) AC_Error("invalid symbol probability");
        distribution[k] = unsigned(sum * (1 << DM__LengthShift));
        sum += p;
        if (tableSize == 0) continue;
        unsigned w = distribution[k] >> tableShift;
        while (s < w) decoderTable[++s] = k - 1;
    }

    if (tableSize != 0)
    {
        decoderTable[0] = 0;
        while (s <= tableSize) decoderTable[++s] = dataSymbols - 1;
    }

    if ((sum < 0.9999) || (sum > 1.0001)) AC_Error("invalid probabilities");
}

AdaptiveDataModel::AdaptiveDataModel()
{
    dataSymbols = 0;
    distribution = 0;
}

AdaptiveDataModel::AdaptiveDataModel(unsigned numberOfSymbols)
{
    dataSymbols = 0;
    distribution = 0;
    setAlphabet(numberOfSymbols);
}

AdaptiveDataModel::~AdaptiveDataModel()
{
    delete [] distribution;
}

void AdaptiveDataModel::setAlphabet(unsigned numberOfSymbols)
{
    if ((numberOfSymbols < 2) || (numberOfSymbols > (1 << 11)))
        AC_Error("invalid number of data symbols");

    /// Assign memory for data model.
    if (dataSymbols != numberOfSymbols)
    {
        dataSymbols = numberOfSymbols;
        lastSymbol = dataSymbols - 1;
        delete [] distribution;
        /// Define size of table for fast decoding
        if (dataSymbols > 16)
        {
            unsigned tableBits = 3;
            while (dataSymbols > (1U << (tableBits + 2))) ++tableBits;
            tableSize  = (1 << tableBits) + 4;
            tableShift = DM__LengthShift - tableBits;
            distribution = new unsigned[2*dataSymbols+tableSize+6];
            decoderTable = distribution + 2 * dataSymbols;
        }
        else /// Small alphabet: no table needed.
        {
            decoderTable = 0;
            tableSize = tableShift = 0;
            distribution = new unsigned[2*dataSymbols];
        }
        symbolCount = distribution + dataSymbols;
        if (distribution == 0) AC_Error("cannot assign model memory");
    }

    reset(); /// Initialize model.
}

void AdaptiveDataModel::update(bool from_encoder)
{
    /// Halve counts when a threshold is reached.
    if ((totalCount += updateCycle) > DM__MaxCount)
    {
        totalCount = 0;
        for (unsigned n = 0; n < dataSymbols; n++)
        totalCount += (symbolCount[n] = (symbolCount[n] + 1) >> 1);
    }

    /// Compute cumulative distribution, decoder table.
    unsigned k, sum = 0, s = 0;
    unsigned scale = 0x80000000U / totalCount;

    if (from_encoder || (tableSize == 0))
        for (k = 0; k < dataSymbols; k++)
        {
            distribution[k] = (scale * sum) >> (31 - DM__LengthShift);
            sum += symbolCount[k];
        }
    else
    {
        for (k = 0; k < dataSymbols; k++)
        {
            distribution[k] = (scale * sum) >> (31 - DM__LengthShift);
            sum += symbolCount[k];
            unsigned w = distribution[k] >> tableShift;
            while (s < w) decoderTable[++s] = k - 1;
        }
        decoderTable[0] = 0;
        while (s <= tableSize) decoderTable[++s] = dataSymbols - 1;
    }
    /// Set frequency of model updates.
    updateCycle = (5 * updateCycle) >> 2;
    unsigned max_cycle = (dataSymbols + 6) << 3;
    if (updateCycle > max_cycle) updateCycle = max_cycle;
    symbolsUntilUpdate = updateCycle;
}

void AdaptiveDataModel::reset()
{
    if (dataSymbols == 0) return;

    /// restore probability estimates to uniform distribution.
    totalCount = 0;
    updateCycle = dataSymbols;
    for (unsigned k = 0; k < dataSymbols; k++) symbolCount[k] = 1;
    update(false);
    symbolsUntilUpdate = updateCycle = (dataSymbols + 6) >> 1;
}
