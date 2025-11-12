#include <argparse/argparse.hpp>
#include <filesystem>
#include "DirectoryData.h"
#include "Compression.h"

bool verbose{false};

int main(int argc, char* argv[])
{
	argparse::ArgumentParser program("logTool", "0.01", argparse::default_arguments::help);

	program.add_argument("-u", "--unpack")
		.help("for unpacking")
		.default_value(false)
		.implicit_value(true);

	program.add_argument("-c", "--compress")
		.help("compress the packed archive with zstd")
		.default_value(false)
		.implicit_value(true);

	program.add_argument("-v", "--verbose")
		.help("display extra messages")
		.default_value(false)
		.implicit_value(true);

	program.add_argument("dir_name")
		.help("The directory to pack or file to unpack").
		required();

	try
	{
		program.parse_args(argc, argv);
	} catch (const std::runtime_error& err) {
		std::cerr << err.what() << std::endl;
		std::cerr << program;
		return 1;
	}

	//std::string work_dir = program.get<std::string>("dir_name");
	bool pack = !program.get<bool>("-u");
	bool compress = program.get<bool>("-c");
	verbose = program.get<bool>("-v");

	DirectoryData dd;

	static constexpr std::array<char, 7> MAGIC_NUMBER_COMPRESS = {'M','Y','D','I','R','X','X'};

	if(pack)
	{
		if (!dd.preProcessSourceDir(program.get<std::string>("dir_name")))
		{
			std::cerr << "Error: Processing source dir failed!\n";
			return 2;
		}

		std::ofstream out("dir_data.bin", std::ios::binary | std::ios::trunc);

		if (!out) {
			std::cerr << "Error: Failed to open file!\n";
			return 3;
		}

		if(compress)
		{
			std::cout << "Compression on.\n";
			out.write(MAGIC_NUMBER_COMPRESS.data(), MAGIC_NUMBER_COMPRESS.size());

			ZstdOStreamBuf zstdStrBuff(out);
			std::ostream outCompress(&zstdStrBuff);
			dd.write(outCompress);
		}
		else
		{
			dd.write(out);
		}

		out.close();
		std::cout << "Data written to dir_data.bin\n";
	}
	else
	{
		fs::path workFile(program.get<std::string>("dir_name"));
		workFile = fs::canonical(workFile);

		if (!fs::exists(workFile) || !fs::is_regular_file(workFile))
		{
			std::cerr << "Error: " << workFile << " is not a file." << std::endl;
			return 3;
		}

		std::ifstream in(workFile, std::ios::binary);

		if (!in) {
			std::cerr << "Error: Failed to open file!\n";
			return 3;
		}

		bool decompress = false;
		{
			std::array<char, MAGIC_NUMBER_COMPRESS.size()> magicNumBuff{};
			in.read(magicNumBuff.data(), MAGIC_NUMBER_COMPRESS.size());
			decompress = (magicNumBuff == MAGIC_NUMBER_COMPRESS);
		}

		if(decompress)
		{
			std::cout << "Data compressed.\n";
			ZstdIStreamBuf zstdStrBuff(in);
			std::istream inDecompress(&zstdStrBuff);

			if(!dd.read(inDecompress))
			{
				std::cerr << "Error: Failed to read compressed file!\n";
				return 5;
			}
		}
		else
		{
			std::cout << "Data not compressed.\n";
			//Not comressed file so moving to start so the 
			//read can check the standard magic number
			in.clear();
			in.seekg(0);
			if(!dd.read(in))
			{
				std::cerr << "Error: Failed to read file!\n";
				return 4;
			}
		}

		in.close();
	}

	std::cout << "Done.\n";

	return 0;
}

