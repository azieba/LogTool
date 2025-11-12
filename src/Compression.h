#include <streambuf>
#include <vector>
#include <zstd.h>

class ZstdOStreamBuf : public std::streambuf
{
public:
    // Constructor takes the target ostream (must outlive this buffer), and optional compression level
    ZstdOStreamBuf(std::ostream &sink);

    ~ZstdOStreamBuf() override;

protected:
    // overflow is called when put area is full or on explicit flush
    int_type overflow(int_type ch) override;

    int sync() override;

private:
    bool flushInput(ZSTD_EndDirective mode);

    void flushStreamEnd();

    std::ostream &outFileStrb_;
    ZSTD_CCtx *cctx_;
    std::vector<char> inBuf_, outBuf_;
};



class ZstdIStreamBuf : public std::streambuf
{
public:
    // Constructor takes the source istream (must outlive this buffer)
    explicit ZstdIStreamBuf(std::istream &source);

    ~ZstdIStreamBuf() override;

protected:
    // Called when get area is exhausted
    int_type underflow() override;

private:
    std::istream& inFileStrb_;
	ZSTD_DCtx* dctx_;
    std::vector<char> inBuf_, outBuf_;

	ZSTD_inBuffer input_{};
	size_t lastZSTDret_{0};
};
