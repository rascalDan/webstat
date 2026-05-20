#include "util.hpp"

namespace WebStat {
	std::error_code
	renameNoExcept(const std::filesystem::path & path, const std::filesystem::path & finalPath) noexcept
	{
		std::error_code error;
		std::filesystem::rename(path, finalPath, error);
		return error;
	}
}
