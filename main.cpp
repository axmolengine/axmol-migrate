/*
non c/c++ objc files is resource file, needs sync manually in cpp-tests:

processing file 1: /home/vmroot/dev/axmol/tests/cpp-tests/Content/extensions/CCControlColourPickerSpriteSheet.plist, len=4137
processing file 2: /home/vmroot/dev/axmol/tests/cpp-tests/Content/extensions/CCControlColourPickerSpriteSheet.png, len=38149
processing file 3: /home/vmroot/dev/axmol/tests/cpp-tests/Content/hd/extensions/CCControlColourPickerSpriteSheet.plist, len=2860
processing file 4: /home/vmroot/dev/axmol/tests/cpp-tests/Content/hd/extensions/CCControlColourPickerSpriteSheet.png, len=83570
*/

// ./core/**/*.h,./core/**/*.cpp,./core/**/*.inl,./core/**/*.mm,./core/**/*.m
// ./extensions/**/*.h,./extensions/**/*.cpp,./extensions/**/*.inl,./extensions/**/*.mm,./extensions/**/*.m
// ./tests/**/*.h,./tests/**/*.cpp,./tests/**/*.inl,./tests/**/*.mm,./tests/**/*.m
// ./templates/**/*.h,./templates/**/*.cpp,./templates/**/*.inl,./templates/**/*.mm,./templates/**/*.m

#include "base/posix_io.h"
#include "base/axstd.h"
#include "yasio/string_view.hpp"
#include <assert.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <regex>
#include <stdexcept>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <fstream>
#include <string>
#include <vector>
#include "fmt/compile.h"

#define AX_MIGRATE_VER "1.1.0"

namespace stdfs = std::filesystem;

using namespace std::string_view_literals;

bool g_use_fuzzy_pattern;
bool g_use_ubo = false;
int totals = 0;
int replaced_totals = 0;
std::vector<std::string_view> chunks;

const std::regex include_re(R"(#(\s)*(include|import)(\s)*"(.)*\b(CC|cc))", std::regex_constants::ECMAScript);
const std::regex include_re_fuzzy(R"(#(\s)*(include|import)(\s)*("|<)(.)*\b(CC|cc))", std::regex_constants::ECMAScript);
const std::regex cmake_re(R"(/CC)", std::regex_constants::ECMAScript | std::regex_constants::icase);

std::string load_file(std::string_view path)
{
	auto fd = posix_open_cxx(path, O_READ_FLAGS);
	if (fd != -1) {
		struct auto_handle {
			~auto_handle() { close(_fd); }
			int _fd;
		} _h { fd };
		size_t len = lseek(fd, 0, SEEK_END);
		if (len > 0) {
			lseek(fd, 0, SEEK_SET);
			std::string content;
			content.reserve(len);
			char buf[512];
			int nb = -1;
			while ((nb = read(fd, buf, sizeof(buf))) > 0) {
				content.append(buf, static_cast<size_t>(nb));
			}
			return content;
		}
	}
	return {};
}

std::vector<std::string> load_file_lines(std::string_view path) {
	std::ifstream file;
	file.open(path.data());

	std::vector<std::string> lines;
	if (file.is_open()) {
		std::string tp;
		while (std::getline(file, tp)) {
			lines.emplace_back(tp + "\n");
		}
		file.close();
	}
	return lines;
}

void save_file_lines(std::string_view path, const std::vector<std::string>& lines) {
	std::ofstream file;
	file.open(path.data());

	if (file.is_open()) {
		for (auto& line : lines) {
			file.write(line.c_str(), line.size());
		}
		file.close();
	}
}

void save_file(std::string_view path, const std::vector<std::string_view>& chunks)
{
	auto fp = fopen(path.data(), "wb");
	if (!fp) {
		throw std::runtime_error("open file fail");
	}
	for (auto& chunk : chunks)
		fwrite(chunk.data(), chunk.length(), 1, fp);
	fclose(fp);
}

bool regex_search_for_replace(const std::string& content, const std::regex& re)
{
	// scan line by line, and put to chunks
	const char* cur_line = content.c_str();
	const char* ptr = cur_line;

	int line_count = 0; // for stats only
	int hints = 0;

	chunks.clear();

	for (;;) {
		++line_count;

		while (*ptr && *ptr != '\n')
			++ptr;

		auto next_line = *ptr == '\n' ? ptr + 1 : ptr;
		std::string_view line { cur_line, static_cast<size_t>(next_line - cur_line)}; // ensure line contains '\n' if not '\0'

		if (line.length() > 1) {
			std::match_results<std::string_view::const_iterator> results;
			if (std::regex_search(line.begin(), line.end(), results, re)
				// we don't want replace c standard header, but will match
				// when use fuzzy pattern
				&& (!g_use_fuzzy_pattern || line.find("<cctype>") == std::string_view::npos)) {
				auto& match = results[0];
				auto first = match.first;
				auto last = match.second;
				// assert(first >= line.data() && first <= &line.back());
				std::string_view word { std::addressof(*first), (size_t) std::distance(first, last)};
				std::string_view chunk1 { cur_line, (size_t)std::distance(cur_line,  std::addressof(*last) - 2)};
				chunks.push_back(chunk1);
				auto chunk2_first = std::addressof(*last);
				if (chunk2_first < next_line) {
					std::string_view chunk2 { chunk2_first, (size_t)std::distance(chunk2_first, next_line) };
					chunks.push_back(chunk2);
				}
				++hints;
			}
			else {
				chunks.push_back(line);
			}
		}
		else { // put empty line directly
			chunks.push_back(line);
		}

		if (*next_line != '\0') {
			ptr = cur_line = next_line;
		}
		else {
			break;
		}
	}

	return !!hints;
}

void process_file(std::string_view file_path, std::string_view file_name, bool is_cmake, bool needs_rename = false)
{
	auto content = load_file(file_path);
	if (content.empty()) {
		throw std::runtime_error("found empty file!");
	}

	if (!is_cmake) {
		// replacing file include stub from CCxxx to xxx, do in editor is better
		if (regex_search_for_replace(content, !g_use_fuzzy_pattern ? include_re : include_re_fuzzy)) {
			printf("replacing c/c++,objc file %d: %s, len=%zu\n", ++totals, file_path.data(), content.size());
			save_file(file_path, chunks);
			++replaced_totals;
		}
		else {
			printf("skipping c/c++,objc file %d: %s, len=%zu\n", ++totals, file_path.data(), content.size());
		}

		if (needs_rename) {
			auto new_file_name = file_name.substr(2);
			std::string new_file_path { file_path.data(), file_path.length() - file_name.length() };
			new_file_path += new_file_name;

			// rename
			int ret = ::rename(file_path.data(), new_file_path.c_str());
			if (ret != 0) {
				throw std::runtime_error("rename file fail");
			}
		}
	}
	else {
		if (regex_search_for_replace(content, cmake_re)) {
			printf("replacing cmake file %d: %s, len=%zu\n", ++totals, file_path.data(), content.size());
			save_file(file_path, chunks);
			++replaced_totals;
		}
		else {
			printf("skip cmake %s not part of axmol engine!\n", file_path.data());
		}
	}
}

void process_folder(std::string_view sub_path)
{
#if defined(_WIN32)
	static std::string exclude = "\\DragonBones\\";
#else
	static std::string exclude = "/DragonBones/";
#endif
	for (const auto& entry : stdfs::recursive_directory_iterator(sub_path)) {
		const auto isDir = entry.is_directory();
		if (entry.is_regular_file()) {
			auto& path = entry.path();
			auto strPath = path.generic_string();
			auto pathname = path.filename();
			auto strName = pathname.generic_string();

			if (strPath.find(exclude) != std::string::npos)
				continue;

			if (cxx20::ic::ends_with(strName, ".h") || cxx20::ic::ends_with(strName, ".hpp") || cxx20::ic::ends_with(strName, ".cpp") || cxx20::ic::ends_with(strName, ".mm") || cxx20::ic::ends_with(strName, ".m") || cxx20::ic::ends_with(strName, ".inl")) {
				process_file(strPath, strName, false, cxx20::ic::starts_with(strName, "CC"));
			}
			else if (cxx20::ic::ends_with(strPath, "CMakeLists.txt")) {
				process_file(strPath, strName, true);
			}
		}
	}
}

// ---------------------------------------- migrate shader glsl 100 to essl 310 for glscc input
namespace Strings {
	inline bool replace_bound(std::string& str, const std::string& from, const std::string& to, int start) {
		size_t start_pos = str.find(from);
		if (start_pos != std::string::npos || start_pos >= start && start_pos + to.length() <= std::string::npos) {
			str.replace(start_pos, from.length(), to);
			return true;
		}
		return false;
	}

	inline bool replace(std::string& str, const std::string& from, const std::string& to) {
		size_t start_pos = str.find(from);
		if (start_pos == std::string::npos)
			return false;
		str.replace(start_pos, from.length(), to);
		return true;
	}

	inline bool wreplace(std::wstring& str, const std::wstring& from, const std::wstring& to) {
		size_t start_pos = str.find(from);
		if (start_pos == std::wstring::npos)
			return false;
		str.replace(start_pos, from.length(), to);
		return true;
	}

	inline std::string replace_const(const std::string str, const std::string& from, const std::string& to) {
		std::string final = str;
		size_t start_pos = final.find(from);
		if (start_pos == std::string::npos)
			return final;
		final.replace(start_pos, from.length(), to);
		return final;
	}

	inline std::wstring wreplace_const(const std::wstring str, const std::wstring& from, const std::wstring& to) {
		std::wstring final = str;
		size_t start_pos = final.find(from);
		if (start_pos == std::wstring::npos)
			return final;
		final.replace(start_pos, from.length(), to);
		return final;
	}

	inline void split_single_char(std::string& str, const char* delim, std::vector<char>& out)
	{
		size_t start;
		size_t end = 0;

		while ((start = str.find_first_not_of(delim, end)) != std::string::npos)
		{
			end = str.find(delim, start);
			out.push_back(str.substr(start, end - start)[0]);
		}
	}

	inline void split(std::string& str, const char* delim, std::vector<std::string>& out)
	{
		size_t start;
		size_t end = 0;

		while ((start = str.find_first_not_of(delim, end)) != std::string::npos)
		{
			end = str.find(delim, start);
			out.push_back(str.substr(start, end - start));
		}
	}

	inline void wsplit(std::wstring& str, const wchar_t* delim, std::vector<std::wstring>& out)
	{
		size_t start;
		size_t end = 0;

		while ((start = str.find_first_not_of(delim, end)) != std::string::npos)
		{
			end = str.find(delim, start);
			out.push_back(str.substr(start, end - start));
		}
	}
}

// libclang APIs
/**
 * Flags that control the creation of translation units.
 *
 * The enumerators in this enumeration type are meant to be bitwise
 * ORed together to specify which options should be used when
 * constructing the translation unit.
 */

 // llvm-15.0.7
#include "clang-c/Index.h"
void* hLibClang = nullptr;
#define DEFINE_CLANG_FUNC(func) decltype(&clang_##func) func
#if defined(_WIN32)
#define GET_CLANG_FUNC(func) func = (decltype(&clang_##func))GetProcAddress((HMODULE)hLibClang, "clang_" #func)
#else
#include <dlfcn.h>
#define GET_CLANG_FUNC(func) func = (decltype(&clang_##func))dlsym(hLibClang, "clang_" #func)
#endif
namespace clang {
	DEFINE_CLANG_FUNC(createIndex);
	DEFINE_CLANG_FUNC(parseTranslationUnit);
	DEFINE_CLANG_FUNC(parseTranslationUnit2);
	DEFINE_CLANG_FUNC(getTranslationUnitCursor);
	DEFINE_CLANG_FUNC(visitChildren);
	DEFINE_CLANG_FUNC(getCursorSpelling);
	DEFINE_CLANG_FUNC(getCursorKindSpelling);
	DEFINE_CLANG_FUNC(getCursorKind);
	DEFINE_CLANG_FUNC(disposeTranslationUnit);
	DEFINE_CLANG_FUNC(disposeIndex);
	DEFINE_CLANG_FUNC(getCString);
	DEFINE_CLANG_FUNC(disposeString);
	DEFINE_CLANG_FUNC(getCursorLocation);
	DEFINE_CLANG_FUNC(getExpansionLocation);
	DEFINE_CLANG_FUNC(getFileName);
	DEFINE_CLANG_FUNC(getCursorPrintingPolicy);
	DEFINE_CLANG_FUNC(PrintingPolicy_setProperty);
	DEFINE_CLANG_FUNC(PrintingPolicy_dispose);
	DEFINE_CLANG_FUNC(getCursorPrettyPrinted);
	DEFINE_CLANG_FUNC(PrintingPolicy_getProperty);

	static void load_lib(const char** argv) {
		if (hLibClang) return;

		// load libclang
#if defined(_WIN32)
		hLibClang = LoadLibrary("libclang.dll");
#else
		std::string exePath = argv[0];
		std::string_view exePathSV{exePath};
		auto slash = exePath.find_last_of('/');
		assert(slash != std::string::npos);
		std::string libclang_file{exePathSV.substr(0, slash + 1)};
#if defined(__linux__)
		libclang_file += "/libclang.so";
#elif defined(__APPLE__)
		libclang_file += "/libclang.dylib";
#endif
		if (stdfs::is_regular_file(libclang_file)) {
			fmt::println("Loading libclang: {}", libclang_file);
			hLibClang = dlopen(libclang_file.c_str(), RTLD_LAZY | RTLD_LOCAL);
	    }
#endif
		if (!hLibClang) {
			fmt::println("load libclang fail.");
			return; // can't load libclang
		}
		GET_CLANG_FUNC(createIndex);
		GET_CLANG_FUNC(parseTranslationUnit);
		GET_CLANG_FUNC(parseTranslationUnit2);
		GET_CLANG_FUNC(getTranslationUnitCursor);
		GET_CLANG_FUNC(visitChildren);
		GET_CLANG_FUNC(getCursorSpelling);
		GET_CLANG_FUNC(getCursorKindSpelling);
		GET_CLANG_FUNC(getCursorKind);
		GET_CLANG_FUNC(disposeTranslationUnit);
		GET_CLANG_FUNC(disposeIndex);
		GET_CLANG_FUNC(getCString);
		GET_CLANG_FUNC(disposeString);
		GET_CLANG_FUNC(getCursorLocation);
		GET_CLANG_FUNC(getExpansionLocation);
		GET_CLANG_FUNC(getFileName);
		GET_CLANG_FUNC(getCursorPrintingPolicy);
		GET_CLANG_FUNC(PrintingPolicy_setProperty);
		GET_CLANG_FUNC(PrintingPolicy_dispose);
		GET_CLANG_FUNC(getCursorPrettyPrinted);
		GET_CLANG_FUNC(PrintingPolicy_getProperty);
	}
}

int replace(std::string& string, const std::string& replaced_key, const std::string& replacing_key)
{
	int count = 0;
	std::string::size_type pos = 0;
	while ((pos = string.find(replaced_key, pos)) != std::string::npos)
	{
		(void)string.replace(pos, replaced_key.length(), replacing_key);
		pos += replacing_key.length();
		++count;
	}
	return count;
}

#if !defined(ARRAYSIZE)
#define ARRAYSIZE(A) (sizeof(A) / sizeof((A)[0]))
#endif

#include <set>

std::string& migrate_strip_outpath(std::string& outpath, const std::set<std::string>& fileNameSet) {

	auto slashpos = outpath.find_last_of("/\\");

	if (slashpos != std::string::npos) {
		std::string_view fileName {outpath.c_str() + slashpos + 1, outpath.size() - slashpos - 1};

		std::string strippedFileName;
		bool is3D = false;
		if (cxx20::ic::starts_with(fileName, "CC2D_")) {
			strippedFileName = fileName.substr(5);
		}
		else if (cxx20::ic::starts_with(fileName, "2D_")) {
			strippedFileName = fileName.substr(3);
		}
		else if (cxx20::ic::starts_with(fileName, "CC3D_")) {
			strippedFileName = fileName.substr(5);
			is3D = true;
		}
		else if (cxx20::ic::starts_with(fileName, "3D_")) {
			strippedFileName = fileName.substr(3);
			is3D = true;
		}
		if (!strippedFileName.empty()) {
			if (is3D) {
				if (fileNameSet.find(strippedFileName) != fileNameSet.end()) {
					auto dotpos = strippedFileName.find_last_of('.');
					if (dotpos != std::string::npos)
						strippedFileName.insert(dotpos, "3D");
					else
						strippedFileName.append("3D");
				}
			}
			if (stdfs::is_regular_file(outpath))
				stdfs::remove(outpath);
			outpath.resize(outpath.size() - fileName.size());
			outpath += strippedFileName;
		}
	}
	return outpath;
}

void insret_define_guard(std::string& shader, size_t& insertpos, std::string_view defineName) {
	std::string define_guard_code = fmt::format("\n\n#if !defined({0})\n#define {0} 0\n#endif\n\n"sv, defineName);
	shader.insert(insertpos, define_guard_code);
	insertpos += define_guard_code.size();
}

extern void migrate_shader_source_one(std::string& shader_source, const std::string& outpath);
extern int migrate_shader_source_one_ast(std::string& shader_source, const std::string& outpath);
void migrate_shader_file_one(std::string_view inpath, const std::set<std::string>& fileNameSet) {

#pragma region parse code file by libclang
	struct ShaderSourceContext {
		bool embedded = false;
		std::vector<std::pair<std::string, std::string>> shaderDecls;
		std::string curVarName;
		stdfs::path fileDir;
		std::string fileName;
		const std::set<std::string>* fileNameSet;
	};
	ShaderSourceContext context;
	context.fileDir = stdfs::path(inpath).parent_path();
	context.fileName = stdfs::path(inpath).filename().generic_string();
	context.fileNameSet = &fileNameSet;
	const char* command_line_args[] = {
		"-xc++",
		"--std=c++17",
	};
	CXIndex index = clang::createIndex(0, 0);
	CXTranslationUnit unit{};
	auto err = clang::parseTranslationUnit2(
		index,
		inpath.data(), command_line_args, (int)ARRAYSIZE(command_line_args),
		nullptr, 0,
		CXTranslationUnit_None, &unit);

	if (unit && err == CXError_Success)
	{
		context.embedded = true;
		CXCursor cursor = clang::getTranslationUnitCursor(unit);
		clang::visitChildren(
			cursor,
			[](CXCursor c, CXCursor parent, CXClientData client_data)
			{
				auto context = (ShaderSourceContext*)client_data;

				CXFile from_file{};
				unsigned line = 0;
				unsigned column = 0;
				unsigned offset = 0;
				auto loc = clang::getCursorLocation(c);
				clang::getExpansionLocation(loc, &from_file, &line, &column, &offset);
				if (!from_file) {
					return CXChildVisit_Continue;
				}
				auto from_spelling = clang::getFileName(from_file);
				std::string fileName = clang::getCString(from_spelling);
				std::string_view fv{fileName};
				auto slash = fv.find_last_of("/\\");
				if (slash != std::string::npos)
					fv.remove_prefix(slash + 1);
				clang::disposeString(from_spelling);

				if (cxx20::ic::iequals(fv, context->fileName)) {
					auto cursorKind = clang::getCursorKind(c);
					auto cursorValue = clang::getCursorSpelling(c);
					if (cursorKind == CXCursorKind::CXCursor_VarDecl) {
						//cursorValue = clang::getCursorSpelling(c);
						context->curVarName = clang::getCString(cursorValue);
					}
					else if (cursorKind == CXCursorKind::CXCursor_StringLiteral) {
						if (!context->curVarName.empty()) {
							//cursorValue = clang::getCursorSpelling(c);

							std::string shaderCode = clang::getCString(cursorValue);
							std::string curVarName {std::move(context->curVarName)};

							// we assume it's engine builtin shaders
							auto idx = curVarName.find_last_of('_');
							if (idx != std::string::npos)
								curVarName[idx] = '.';
							auto path = context->fileDir;
							path += "/";
							path += curVarName;

							replace(shaderCode, "\\n", "\n");
							replace(shaderCode, "\"", "");
							replace(shaderCode, "\\t", "\t");
							context->shaderDecls.emplace_back(path.generic_string(), std::move(shaderCode));
						}
					}
					clang::disposeString(cursorValue);
				}
				return CXChildVisit_Recurse;
			},
			&context);


		clang::disposeTranslationUnit(unit);
	}
	else {
		// read plain shader code line by line
		std::fstream file;
		file.open(inpath.data());

		std::string shader;

		if (file.is_open()) {
			std::string tp;
			while (std::getline(file, tp)) {
				shader += tp + "\n";
			}
			file.close();
		}

		context.shaderDecls.emplace_back(inpath, shader);
	}
	clang::disposeIndex(index);
#pragma endregion

	if (context.shaderDecls.size() == 1) // single decl, use inpath
		context.shaderDecls[0].first = inpath;
	else if (context.shaderDecls.empty()) {
		context.shaderDecls.emplace_back(
			inpath,
			load_file(inpath));
	}
	int hints = 0;
	for (auto& item : context.shaderDecls) {
		auto& shader = item.second;
		if (g_use_ubo) {
			auto& outpath = migrate_strip_outpath(item.first, fileNameSet);
			hints += migrate_shader_source_one_ast(shader, outpath);
			fmt::println("Convert {} to 310 es done.", outpath);
		}
		else {
			auto& outpath = item.first;
			migrate_shader_source_one(shader, outpath);
			fmt::println("Convert {} to 310 es done.", outpath);
			++hints;
		}
	}
	if (hints && context.shaderDecls.size() > 1)
		stdfs::remove(inpath);
}

bool is_in_filter(std::string_view fileName, const std::vector<std::string_view>& filterList) {
	for (auto& filter : filterList)
		if (cxx20::ic::ends_with(fileName, filter))
			return true;
	return false;
}

void migrate_shader_files_in_dir(std::string_view dir, const std::vector<std::string_view>& filterList, const char** argv) {
	clang::load_lib(argv);

	std::set<std::string> fileNameSet;
	for (const auto& entry : stdfs::recursive_directory_iterator(dir)) {
		const auto isDir = entry.is_directory();
		if (entry.is_regular_file()) {
			auto& path = entry.path();
			auto strPath = path.generic_string();
			auto pathname = path.filename();
			auto strName = pathname.generic_string();

			if (is_in_filter(strName, filterList))
				fileNameSet.insert(strName);
		}
	}

	for (const auto& entry : stdfs::recursive_directory_iterator(dir)) {
		const auto isDir = entry.is_directory();
		if (entry.is_regular_file()) {
			auto& path = entry.path();
			auto strPath = path.generic_string();
			auto pathname = path.filename();
			auto strName = pathname.generic_string();

			if (is_in_filter(strName, filterList))
				migrate_shader_file_one(strPath, fileNameSet);
		}
	}
}

/*
usage:
   sources-migrate <source-dir> [--fuzzy]
*/

int do_migrate(int argc, const char** argv)
{
	printf("axmol-migrate version %s\n\n", AX_MIGRATE_VER);

	if (argc < 3) {
		printf("Invalid parameter, usage: axmol-migrate <type> [--fuzzy] [--for-engine]  --source-dir <source_dir> [--filters .frag;.vert;.vsh;.fsh][--use-ubo]\n\ttype: cpp, shader");
		return -1;
	}

	const char* type = argv[1];

	// parse args
	bool migrateEngine = false;
	const char* sourceDir = nullptr;
	auto&& filterList = strcmp(type, "cpp") == 0 ? std::vector<std::string_view>{".h", ".cpp", ".hpp", ".mm", ".m"} : std::vector<std::string_view>{ ".vert", ".frag", ".vsh", ".fsh" };
	for (int argi = 2; argi < argc; ++argi) {
		if (strcmp(argv[argi], "--fuzzy") == 0) {
			g_use_fuzzy_pattern = true;
		}
		else if (strcmp(argv[argi], "--for-engine") == 0) {
			migrateEngine = true;
		}
		else if (strcmp(argv[argi], "--source-dir") == 0) {
			++argi;
			if (argi < argc) {
				sourceDir = argv[argi];
				if (!stdfs::is_directory(sourceDir)) {
					fprintf(stderr, "The source directory: %s not exist\n", sourceDir);
					return -1;
				}
			}
		}
		else if (strcmp(argv[argi], "--filters") == 0) {
			++argi;
			if (argi < argc) {
				auto strFilters = argv[argi];
				axstd::split_cb(strFilters, strlen(strFilters), ';', [&](const char* s, const char* e) {
					std::string_view filter{s, static_cast<size_t>(e - s)};
					if (!filter.empty() && std::find_if(filterList.begin(), filterList.end(), [=](const std::string_view& elem) {return cxx20::ic::iequals(elem, filter); }) == filterList.end()) {
						filterList.emplace_back(filter);
					}
					});
			}
		}
		else if (strcmp(argv[argi], "--use-ubo") == 0) {
			g_use_ubo = true;
		}
	}

	if (strcmp(type, "cpp") == 0) {

		// perform migrate
		if (!migrateEngine) {
			if (!sourceDir) {
				printf("Invalid source dir not specified for to migrate project of axmol engine!\n");
				return -1;
			}
			printf("Migrating project sources in %s\n", sourceDir);
			auto start = std::chrono::steady_clock::now();
			process_folder(sourceDir);
			auto diff = std::chrono::steady_clock::now() - start;
			printf("Migrate done, replaced totals: %d, total cost: %.3lf(ms)\n", replaced_totals,
				std::chrono::duration_cast<std::chrono::microseconds>(diff).count() / 1000.0);
		}
		else {
			if (!sourceDir) {
				sourceDir = getenv("AX_ROOT");
			}
			if (!sourceDir || !stdfs::is_directory(sourceDir)) {
				printf("No valid source dir to migrate axmol engine!\n");
				return -1;
			}

			printf("Migrating axmol engine sources in %s\n", sourceDir);
			auto start = std::chrono::steady_clock::now();

			// 921 .h, .cpp, .mm, .m
			process_folder(std::string { sourceDir } + "/core");
			process_folder(std::string { sourceDir } + "/extensions");
			process_folder(std::string { sourceDir } + "/tests");

			auto diff = std::chrono::steady_clock::now() - start;
			printf("Migrate done, replaced totals: %d, total cost: %.3lf(ms)\n", replaced_totals,
				std::chrono::duration_cast<std::chrono::microseconds>(diff).count() / 1000.0);
		}
	}
	else if (strcmp(type, "shader") == 0)
	{ // migrate glsl 100 to essl 310
		if (sourceDir) {
			migrate_shader_files_in_dir(sourceDir, filterList, argv);
		}
	}
	else if (strcmp(type, "code") == 0) {
		auto axroot = getenv("AX_ROOT");
        if(axroot) { // batch modify axmol engine shaders.cpp sources to shader name
			std::string shaders_cpp = axroot;
			if (shaders_cpp.back() != '/') shaders_cpp.push_back('/');
			shaders_cpp += "core/renderer/Shaders.cpp";

			auto lines = load_file_lines(shaders_cpp);
			std::regex shader_varexp(R"([a-zA-Z_]+\w+(vert|frag)\b)");
			for (auto& line : lines) {
				std::match_results<std::string::const_iterator> results;
				if (std::regex_search(line, results, shader_varexp))
				{
					auto& match = results[0];
					auto first = match.first;
					auto last = match.second;
				
					auto count = last - first;

					std::string newexp{first, last};
				
					newexp += " = \"";
					std::string_view varName{std::addressof(*first), (size_t)std::distance(std::addressof(*first), std::addressof(*last))};
					varName.remove_suffix(3);
					newexp += varName;
					newexp += 's';
					newexp += "\"";
				
					line.replace(std::distance(line.cbegin(), first), count, newexp);
				}
			}

			save_file_lines(shaders_cpp, lines);
        }
	}

	return 0;
}

int main(int argc, const char** argv)
{
	if (do_migrate(argc, argv) == 0)
		return 0;
#if defined(_DEBUG) || !defined(NDEBUG) // test only
#pragma message("building debug")
	auto axroot = getenv("AX_ROOT");
	if (axroot) {
		std::string ax_shader_root = axroot;
		if (ax_shader_root.back() != '/') ax_shader_root.push_back('/');
		ax_shader_root += "core/renderer/shaders";
		const char* test_args[] = {
			argv[0],
			"shader",
			"--source-dir",
			ax_shader_root.c_str()
		};
		if (stdfs::is_directory(ax_shader_root))
		    do_migrate((int)ARRAYSIZE(test_args), test_args);
	}
	return 0;
#endif
}
