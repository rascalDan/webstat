#include "util.hpp"
#include <ext/stdio_filebuf.h>
#include <ext/stdio_sync_filebuf.h>
#include <fstream>

namespace WebStat {
	std::error_code
	renameNoExcept(const std::filesystem::path & path, const std::filesystem::path & finalPath) noexcept
	{
		std::error_code error;
		std::filesystem::rename(path, finalPath, error);
		return error;
	}

	int
	fileno(const std::istream & stream)
	{
		// Based loosely on code from https://www.ginac.de/~kreckel/fileno/
		using CharT = std::istream::char_type;
		using Traits = std::istream::traits_type;

		using FileBuf = std::basic_filebuf<CharT, Traits>;
		using StdioFileBuf = __gnu_cxx::stdio_filebuf<CharT, Traits>;
		using StdioSyncFileBuf = __gnu_cxx::stdio_sync_filebuf<CharT, Traits>;

		// This applies to cin, cout, and cerr when not synced with stdio:
		if constexpr (!std::is_base_of_v<FileBuf, StdioFileBuf>) {
			if (const auto * fbuf = dynamic_cast<StdioFileBuf *>(stream.rdbuf())) {
				return fbuf->native_handle();
			}
		}

		// This applies to filestreams:
		if (const auto * bbuf = dynamic_cast<FileBuf *>(stream.rdbuf())) {
			return bbuf->native_handle();
		}

		// This applies to cin, cout, and cerr when synced with stdio:
		if (auto * sbuf = dynamic_cast<StdioSyncFileBuf *>(stream.rdbuf())) {
			return ::fileno(sbuf->file());
		}

		throw std::system_error {std::make_error_code(std::errc::bad_file_descriptor),
				"Couldn't get underlying file descriptor from stream"};
	}
}
