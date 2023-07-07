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

#define AX_MIGRATE_VER "1.1.0"

namespace stdfs = std::filesystem;

using namespace std::string_view_literals;

bool g_use_fuzzy_pattern;
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
		std::string_view line { cur_line, next_line }; // ensure line contains '\n' if not '\0'

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
				std::string_view word { first, last };
				std::string_view chunk1 { cur_line, std::addressof(*last) - 2 };
				chunks.push_back(chunk1);
				auto chunk2_first = std::addressof(*last);
				if (chunk2_first < next_line) {
					std::string_view chunk2 { chunk2_first, next_line };
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
#include <fstream>
#include <string>
#include <vector>
#include "fmt/compile.h"
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
#define GET_CLANG_FUNC(func) clang::func = (decltype(&clang_##func))GetProcAddress((HMODULE)hLibClang, "clang_" #func)
#else
#include <dlfcn.h>
#define GET_CLANG_FUNC(func) clang::func = (decltype(&clang_##func))dlsym(hLibClang, "clang_" #func)
#endif
namespace clang {
	DEFINE_CLANG_FUNC(createIndex);
	DEFINE_CLANG_FUNC(parseTranslationUnit);
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

void migrate_shader_one(std::string_view inpath) {

#pragma region parse code file by libclang
	struct ShaderSourceContext {
		bool embedded = false;
		std::vector<std::pair<std::string, std::string>> shaderDecls;
		std::string curVarName;
		stdfs::path fileDir;
		std::string fileName;
	};
	ShaderSourceContext context;
	context.fileDir = stdfs::path(inpath).parent_path();
	context.fileName = stdfs::path(inpath).filename().generic_string();
	const char* command_line_args[] = {
		"-xc++",
		"--std=c++11",
	};
	CXIndex index = clang::createIndex(0, 0);
	CXTranslationUnit unit = clang::parseTranslationUnit(
		index,
		inpath.data(), command_line_args, (int)ARRAYSIZE(command_line_args),
		nullptr, 0,
		CXTranslationUnit_None);

	if (unit)
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
				if (!from_file) return CXChildVisit_Continue;
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
						cursorValue = clang::getCursorSpelling(c);
						context->curVarName = clang::getCString(cursorValue);
					}
					else if (cursorKind == CXCursorKind::CXCursor_StringLiteral) {
						if (!context->curVarName.empty()) {
							cursorValue = clang::getCursorSpelling(c);
							std::string shaderCode = clang::getCString(cursorValue);
							// we assume it's engine builtin shaders
							auto idx = context->curVarName.find_last_of('_');
							if (idx != std::string::npos)
								context->curVarName[idx] = '.';
							auto path = context->fileDir;
							path += "/";
							if (cxx20::ic::starts_with(context->curVarName, "CC2D_")) {
								path += "2D_";
								path += context->curVarName.substr(5);
							}
							else if (cxx20::ic::starts_with(context->curVarName, "CC3D_")) {
								path += "3D_";
								path += context->curVarName.substr(5);
							}
							else
								path += context->curVarName;
							context->curVarName.clear();

							replace(shaderCode, "\\n", "\n");
							replace(shaderCode, "\"", "");
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

	for (auto& item : context.shaderDecls) {
		auto& shader = item.second;
		std::regex verexp(R"(#version)");
		if (!std::regex_search(shader, verexp)) {
			shader.insert(0, "#version 310 es\nprecision highp float;\nprecision highp int;\n");
		}
		else {
			std::cout << "The shader " << inpath << " is already 310 es compatible!\n";
			continue;
		}

		// GL_ES Macros might actually be important.
		//std::regex exp(R"(#ifdef GL_ES([\s\S]*?)\#else)");
		//if (std::regex_search(shader, exp)) {
		//    shader = std::regex_replace(shader, exp, "");
		//    while (Strings::replace(shader, "#endif", ""));
		//}

		std::regex fragexp(R"(gl_Position)");
		bool isFragmentShader = !std::regex_search(shader, fragexp);

		int layoutLocation = 0;
		int layoutLocationOut = 0;

		while (Strings::replace(shader, "attribute", fmt::format("layout (location = {}) in",
			std::to_string(layoutLocation++))));
		layoutLocation--;

		while (Strings::replace(shader, "varying", fmt::format("layout (location = {}) {}",
			std::to_string((isFragmentShader ? (layoutLocation++) : (layoutLocationOut++))), isFragmentShader ? "in" : "out")));
		layoutLocation--;

		while (Strings::replace(shader, "gl_FragColor", "FragColor"));

		while (Strings::replace(shader, "texture2D(", "texture("));
		while (Strings::replace(shader, "texture2D (", "texture("));

		while (Strings::replace(shader, "uniform sampler2D", fmt::format("layout(location = {}, binding = 0) uniform  sampler2D", std::to_string(layoutLocation++))));
		while (Strings::replace(shader, "uniform  sampler2D", "uniform sampler2D"));
		layoutLocation--;

		if (isFragmentShader) {
			Strings::replace(shader, "void main", fmt::format("layout (location = {}) out vec4 FragColor;\nvoid main",
				std::to_string(layoutLocationOut++)));
			layoutLocationOut--;
		}

		std::vector<std::string> lines;
		Strings::split(shader, "\n", lines);

		int currentUBO = 0;
		int currentUBOBinding = 0;
		auto sorroundUBOBlock = [&]()
		{
			bool isBlock = false;
			for (int i = 0; i < lines.size(); i++) {
				if (lines[i].starts_with("uniform ")) {
					Strings::replace(lines[i], "uniform ", "    ");

					if (!isBlock) {
						std::string uboBlockName = "Block_" + std::to_string(currentUBO++);
						std::string insertion = "layout(std140, binding = " + std::to_string(currentUBOBinding++) + ") uniform " + uboBlockName + " {\n";
						lines[i].insert(0, insertion);
						isBlock = true;
					}
				}
				else if (isBlock) {
					lines[i].insert(0, "};\n");
					isBlock = false;
				}
			}
		};

		sorroundUBOBlock();
		shader.clear();
		for (int i = 0; i < lines.size(); i++) {
			shader += lines[i] + "\n";
		}

#pragma region WriteFile

		std::regex regex(R"(\n{3,})");
		shader = std::regex_replace(shader, regex, "\n");

		if (shader[0] == '\n') shader = shader.substr(1);

		std::ofstream modified;
		auto outpath = item.first;
		modified.open(outpath, std::ofstream::out | std::ofstream::trunc);
		modified << shader;
		modified.close();

		std::cout << "Convert " << outpath << " to 310 es done.\n";

#pragma endregion
	}
	if (context.shaderDecls.size() > 1)
		stdfs::remove(inpath);
}

bool is_in_filter(std::string_view fileName, const std::vector<std::string_view>& filterList) {
	for (auto& filter : filterList)
		if (cxx20::ic::ends_with(fileName, filter))
			return true;
	return false;
}

static std::string _checkPath(const char* path) {
	std::string ret;
	ret.resize(PATH_MAX - 1);
	int n = readlink(path, &ret.front(), PATH_MAX);
	if (n > 0) {
		ret.resize(n);
		return ret;
	}
	return std::string{};
}

void migrate_shader_in_dir(std::string_view dir, const std::vector<std::string_view>& filterList) {
	// load libclang
#if defined(_WIN32)
	hLibClang = LoadLibrary("libclang.dll");
#else
	auto exePath = _checkPath("/proc/self/exe");
	std::string_view exePathSV{exePath};
	auto slash = exePath.find_last_of('/');
	assert(slash != std::string::npos);
	std::string exeDir{exePathSV.substr(0, slash + 1)};
	exeDir += "/libclang.so";
	hLibClang = dlopen(exeDir.c_str(), RTLD_LAZY | RTLD_LOCAL);
#endif
	if (!hLibClang) {
		fmt::println("load libclang fail.");
		return; // can't load libclang
	}
	GET_CLANG_FUNC(createIndex);
	GET_CLANG_FUNC(parseTranslationUnit);
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

	for (const auto& entry : stdfs::recursive_directory_iterator(dir)) {
		const auto isDir = entry.is_directory();
		if (entry.is_regular_file()) {
			auto& path = entry.path();
			auto strPath = path.generic_string();
			auto pathname = path.filename();
			auto strName = pathname.generic_string();

			for (auto& filter : filterList)
				if (cxx20::ic::ends_with(strName, filter))
					break;

			if (is_in_filter(strName, filterList))
				migrate_shader_one(strPath);
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
		printf("Invalid parameter, usage: axmol-migrate <type> [--fuzzy] [--for-engine]  --source-dir <source_dir> [--filters .frag;.vert;.vsh;.fsh]\type: cpp, shader");
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
					std::string_view filter{s, e};
					if (!filter.empty() && std::find_if(filterList.begin(), filterList.end(), [=](const std::string_view& elem) {return cxx20::ic::iequals(elem, filter); }) == filterList.end()) {
						filterList.emplace_back(filter);
					}
					});
			}
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
			migrate_shader_in_dir(sourceDir, filterList);
		}
	}

	return 0;
}

int main(int argc, const char** argv)
{
	// test
#if 0
	const char* test_args[] = {
		"/proc/self/exe",
		"shader",
		"--source-dir",
		"/home/vmroot/dev/axmol/core/renderer/shaders"
	};
	do_migrate((int)ARRAYSIZE(test_args), test_args);
#endif
	return do_migrate(argc, argv);
}
