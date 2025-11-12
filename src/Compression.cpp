#include "Compression.h"
#include <iostream>
#include <cassert>

extern bool verbose;

ZstdOStreamBuf::ZstdOStreamBuf(std::ostream &sink):
	outFileStrb_(sink),
	cctx_(ZSTD_createCCtx()),
	inBuf_(ZSTD_CStreamInSize()),
	outBuf_(ZSTD_CStreamOutSize())
{
	assert(cctx_ != nullptr);

	if(verbose) std::cout << "ZstdOStreamBuf: inBuff.size=" << inBuf_.size() << ", outBuff.size=" << outBuf_.size() << '\n';

	ZSTD_CCtx_setParameter(cctx_, ZSTD_c_checksumFlag, 1);

	// set buffer pointers (put area) to our inBuf
	setp(inBuf_.data(), inBuf_.data() + inBuf_.size());
}

ZstdOStreamBuf::~ZstdOStreamBuf()
{
	sync();         // flush pending data
	flushStreamEnd();
	ZSTD_freeCCtx(cctx_);
}

ZstdOStreamBuf::int_type ZstdOStreamBuf::overflow(int_type ch)
{
	if (flushInput(ZSTD_e_continue) == false) {
		return traits_type::eof();
	}

	if (ch != traits_type::eof()) {
		*pbase() = static_cast<char>(ch);
		pbump(1);
	}

	return ch;
}

int ZstdOStreamBuf::sync()
{
	if (flushInput(ZSTD_e_flush) == false)
	{
		return -1;
	}

	return 0;
}

bool ZstdOStreamBuf::flushInput(ZSTD_EndDirective mode)
{
	char* inData = inBuf_.data();
	auto inSize = pptr() - pbase();
	ZSTD_inBuffer input{ inData, static_cast<size_t>(inSize), 0 };

	while (input.pos < input.size)
	{
		ZSTD_outBuffer output{ outBuf_.data(), outBuf_.size(), 0 };
		size_t ret = ZSTD_compressStream2(cctx_, &output, &input, mode);
		assert (!ZSTD_isError(ret));

		if (output.pos > 0) {
			outFileStrb_.write((char*)output.dst, output.pos);
			if (!outFileStrb_) return false;
		}
	}

	// this acutally resets the put pointer
	setp(pbase(), epptr());
	return true;
}

void ZstdOStreamBuf::flushStreamEnd()
{
	// end-of-stream: send final frame
	bool done = false;
    ZSTD_inBuffer emptyInput{ nullptr, 0, 0 };
	while (!done)
	{
		ZSTD_outBuffer output{ outBuf_.data(), outBuf_.size(), 0 };
		size_t ret = ZSTD_compressStream2(cctx_, &output, /* empty input */ &emptyInput, ZSTD_e_end);
		assert(!ZSTD_isError(ret));
		if (output.pos > 0) {
			outFileStrb_.write((char*)output.dst, output.pos);
		}
		if (ret == 0) done = true;
	}
	outFileStrb_.flush();

	std::cout << "Compression completed.\n";
}




ZstdIStreamBuf::ZstdIStreamBuf(std::istream &source):
	inFileStrb_(source),
	dctx_(ZSTD_createDCtx()),
	inBuf_(ZSTD_DStreamInSize()),
	outBuf_(ZSTD_DStreamOutSize())
{
	assert(dctx_);

	if(verbose) std::cout << "ZstdIStreamBuf: inBuff.size=" << inBuf_.size() << ", outBuff.size=" << outBuf_.size() << '\n';

	input_.src = inBuf_.data();

	setg(outBuf_.data(), outBuf_.data(), outBuf_.data());
};

ZstdIStreamBuf::~ZstdIStreamBuf()
{
	ZSTD_freeDCtx(dctx_);

	std::cout << "Decompression completed.\n";
};

ZstdIStreamBuf::int_type ZstdIStreamBuf::underflow()
{
	if (gptr() < egptr()) return traits_type::to_int_type(*gptr());

	ZSTD_outBuffer output{ outBuf_.data(), outBuf_.size(), 0 };

	while(output.pos == 0)
	{
        if (input_.pos == input_.size) {
            inFileStrb_.read(inBuf_.data(), inBuf_.size());
            input_.size = inFileStrb_.gcount();
            input_.pos = 0;

            if (input_.size == 0) 
			{
				//end of data but ZSD has still something to write
				//This should not happen
				assert(lastZSTDret_ == 0);
                return traits_type::eof();
            }
        }

        lastZSTDret_ = ZSTD_decompressStream(dctx_, &output, &input_);
        assert(!ZSTD_isError(lastZSTDret_));
	}

	// Set get area pointers
	setg(outBuf_.data(), outBuf_.data(), outBuf_.data() + output.pos);

	return traits_type::to_int_type(*gptr());
}
