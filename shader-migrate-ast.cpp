#include <string>
#include <regex>
#include <vector>
#include <map>
#include <set>
#include <stack>
#include <assert.h>
#include "fmt/compile.h"
#include "yasio/byte_buffer.hpp"
#include "yasio/string_view.hpp"
#include "yasio/object_pool.hpp"
#include "xxhash/xxhash.h"

using namespace std::string_view_literals;


// GLSL100 shader language regex expressions
static const std::regex attribute_decl_exp(R"(attribute\s+.*)", std::regex_constants::ECMAScript);
static const std::regex varying_decl_exp(R"(varying\s+.*)", std::regex_constants::ECMAScript);
static const std::regex uniform_decl_exp(R"(uniform\s+.*)", std::regex_constants::ECMAScript);
static const std::regex sampler_decl_exp(R"(uniform\s+(sampler2D|samplerCube)\s+.*)", std::regex_constants::ECMAScript); // sampler2D or samplerCube
static const std::regex var_name_exp(R"([\w_]+[a-zA-Z0-9_]*\s*(\[\s*[\w_]+[a-zA-Z0-9_]\s*\]\s*)?;)", std::regex_constants::ECMAScript);
static const std::regex remove_var_array_exp(R"([\w_]+[a-zA-Z0-9_]*\s*;)", std::regex_constants::ECMAScript);
static const std::regex func_decl_exp(R"([\w_]+[a-zA-Z0-9_]*\s+[\w_]+[a-zA-Z0-9_]*\s*\(.*\))", std::regex_constants::ECMAScript);
static const std::regex main_decl_exp(R"(void\s+main\s*\()", std::regex_constants::ECMAScript);
static const std::regex pp_define_exp(R"(#\s*define\s+.+)", std::regex_constants::ECMAScript);
static const std::regex pp_block_if(R"(#\s*if)", std::regex_constants::ECMAScript);
static const std::regex pp_block_elif(R"(#\s*elif)", std::regex_constants::ECMAScript);
static const std::regex pp_block_else(R"(#\s*else)", std::regex_constants::ECMAScript);
static const std::regex pp_block_endif(R"(#\s*endif)", std::regex_constants::ECMAScript);
static const std::regex c_symbol_exp(R"([\w_]+[a-zA-Z0-9_]*)", std::regex_constants::ECMAScript);

// vec4 sample = texture
static const std::regex reserved_sample_decl_expr(R"(vec4\s+sample\s*=)", std::regex_constants::ECMAScript);
static const std::regex reserved_sample_ref_expr(R"(.*=\s*sample\s*\.)", std::regex_constants::ECMAScript);

static const std::regex sample_texture2d_exp(R"(texture2D\s*\()", std::regex_constants::ECMAScript);
static const std::regex sample_texturecube_exp(R"(.*=.*\s*textureCube\s*\()", std::regex_constants::ECMAScript);

static const std::regex gl_FragColor_exp(R"(gl_FragColor)", std::regex_constants::ECMAScript);

// preprocesser check
/*
* #if XXX > YYY XXX < YYY XXX == YYY XXX <= YYY XXX >= YYY
*/
static const std::regex pp_cond_expr(R"([\w_]+[a-zA-Z0-9_]*\s*(<|>|==))");
/*
* uniform block: the name of uniform block must not same between vert and .frag
*   uniform block naming rule:
*       - vert: vs_ub
*       - vert: fs_ub
* translate #ifdef GL_ES
*/

struct ASTNode;
enum PPFlag {
	ppNone = 0,
	ppStart = 1,
	ppElif = 2,
	ppElse = 4,
	ppEnd = 8,
	ppGLES = 1 << 17,
};

struct ASTNode {
	ASTNode() {}
	std::string_view name = "*"sv; // root is global block
	std::string_view ppend = ""sv;

	// std::vector<std::string_view> uniforms; // line code
	// std::vector<std::string_view> lines; // other line code
	int lineIndex = 0; // record line index we can insert code correct postion
	bool isNonSamplerUniform = false;
	bool isFuncDecl = false;
	bool isMainDecl = false;
	int ppFlag = PPFlag::ppNone;

	ASTNode* parent = nullptr;
	std::vector<ASTNode*> children;
	bool toRemove = false;

	void addChild(ASTNode* child) {
		children.push_back(child);
		child->parent = this;
	}
};

yasio::object_pool<ASTNode> _pool;

struct GlslParseContext {


	bool _is_frag = false;

	std::map<int64_t, yasio::sbyte_buffer> _strCache;

	std::set<std::string_view> _defines;
	std::set<std::string_view> _usedDefines;

	ASTNode* _AST{};
	std::stack<ASTNode*> _stack;

	// process preprocessor #if #else #endif for attribute, varying
	// layout(location = xxx)
	std::map<int, int> _locationMap;

	// process preprocessor #if #else #endif for uniform(non-sampler, sampler2D, samplerCube)
	// layout([std140], binding = xxx)
	// std::map<int, int> _bindingIndexMap;

	// need map
	int _curInLoc = 0;
	int _curOutLoc = 0;

	int _samplerBindingIndex = 0;
	// int _ubBindingIndex = 0; // alwasy 0

	// uniform block should insert before any funcs
	int _firstFuncNum = -1;

	GlslParseContext()
	{
		_AST = createNode(""sv, nullptr);
	}
	~GlslParseContext() {
		destroyAST(_AST);
	}

	ASTNode* createNode(std::string_view line, ASTNode* parent) {
		auto node = _pool.create();
		node->name = line;
		if (parent) {
			parent->addChild(node);
		}
		return node;
	}
	void destroyAST(ASTNode* p) {
		for (auto c : p->children)
			destroyAST(c);
		_pool.destroy(p);
	}

	bool parseAST(std::string& shader_source, const std::string& outpath) {
		_is_frag = cxx20::ic::ends_with(outpath, ".frag"sv) || shader_source.find("gl_FragColor") != std::string::npos;

		_stack.push(_AST);

		//if (!is_frag) { // verts
						// scan line by line, and put to code_lines
		const char* cur_line = shader_source.c_str();
		const char* ptr = cur_line;

		int line_count = 0; // for stats only
		int hints = 0;

		// code_lines.clear();

		for (;;) {
			++line_count;

			while (*ptr && *ptr != '\n')
				++ptr;

			auto next_line = *ptr == '\n' ? ptr + 1 : ptr;
			std::string_view line { cur_line, static_cast<size_t>(next_line - cur_line)}; // ensure line contains '\n' if not '\0'

			size_t matchOffset = 0;

			if (line.length() > 1) {
				std::match_results<std::string_view::const_iterator> results;
				if (std::regex_search(line.begin(), line.end(), results, attribute_decl_exp)) { // vert: attribute ...
					std::string mutableLine = getMatchedMutableLine(results, line, matchOffset);
					mutableLine.replace(matchOffset, sizeof("attribute") - 1, "in"); //
					insertLocation(mutableLine, _curInLoc);
					replace_precision_qualifiers(mutableLine);
					createNode(cachestr(mutableLine), _stack.top());
				}
				else if (std::regex_search(line.begin(), line.end(), results, varying_decl_exp)) { // vert/frag: varying ...
					std::string mutableLine = getMatchedMutableLine(results, line, matchOffset);

					mutableLine.replace(matchOffset, sizeof("varying") - 1, !_is_frag ? "out" : "in");
					if (!_is_frag)
						insertLocation(mutableLine, _curOutLoc);
					else
						insertLocation(mutableLine, _curInLoc);
					replace_precision_qualifiers(mutableLine);
					createNode(cachestr(mutableLine), _stack.top());
				}
				else if (std::regex_search(line.begin(), line.end(), results, sampler_decl_exp)) { // frag sampler2D or samplerCube
					std::string mutableLine = getMatchedMutableLine(results, line, matchOffset);
					mutableLine.insert(0, fmt::format("layout(binding = {}) ", _samplerBindingIndex++));
					createNode(cachestr(mutableLine), _stack.top());
				}
				else if (std::regex_search(line.begin(), line.end(), results, sample_texture2d_exp)) { // texture2D(
					std::string mutableLine = getMatchedMutableLine(results, line, matchOffset);
					replace(mutableLine, "texture2D", "texture");
					replace_once(mutableLine, "sample", "texColor");
					replace_once(mutableLine, "gl_FragColor", "FragColor");
					createNode(cachestr(mutableLine), _stack.top());
				}
				else if (std::regex_search(line.begin(), line.end(), results, sample_texturecube_exp)) { // textureCube(
					std::string mutableLine = getMatchedMutableLine(results, line, matchOffset);
					replace(mutableLine, "textureCube", "texture");
					replace_once(mutableLine, "gl_FragColor", "FragColor");
					createNode(cachestr(mutableLine), _stack.top());
				}
				else if (std::regex_search(line.begin(), line.end(), results, gl_FragColor_exp)) { // vec4 sample = 
					std::string mutableLine = getMatchedMutableLine(results, line, matchOffset);
					replace_once(mutableLine, "gl_FragColor", "FragColor");
					createNode(cachestr(mutableLine), _stack.top());
				}
				else if (std::regex_search(line.begin(), line.end(), results, reserved_sample_decl_expr)) { // vec4 sample = 
					std::string mutableLine = getMatchedMutableLine(results, line, matchOffset);
					replace_once(mutableLine, "sample", "texColor");
					createNode(cachestr(mutableLine), _stack.top());
				}
				else if (std::regex_search(line.begin(), line.end(), results, reserved_sample_ref_expr)) { // fix syntax symbol: sample is reserved
					std::string mutableLine = getMatchedMutableLine(results, line, matchOffset);
					replace_once(mutableLine, "sample", "texColor");
					createNode(cachestr(mutableLine), _stack.top());
				}
				else if (std::regex_search(line.begin(), line.end(), results, uniform_decl_exp)) { // vert/frag: uniforms
					auto uniformOffset = std::distance(line.cbegin(), results[0].first);
					auto commentOffset = line.find("//");
					if (commentOffset == std::string::npos || uniformOffset < commentOffset) { // not comment
						auto node = createNode(line, _stack.top());
						node->isNonSamplerUniform = true;
					}
					else { // a nomral line
						createNode(line, _stack.top());
					}
				}
				else if (std::regex_search(line.begin(), line.end(), results, pp_define_exp)) {
					createNode(line, _stack.top());

					auto& match = results[0];
					auto first = std::addressof(*match.first) + sizeof("#define"); // start search next char of #define
					auto count = static_cast<size_t>(std::distance(match.first, match.second));

					std::string_view subtext{first, count};
					std::match_results<std::string_view::const_iterator> results_;
					assert(std::regex_search(subtext.begin(), subtext.end(), results_, c_symbol_exp));
					auto& match_ = results_[0];
					first = std::addressof(*match_.first);
					count = static_cast<size_t>(std::distance(match_.first, match_.second));
					_defines.insert(std::string_view{first, count});
				}
				else if (std::regex_search(line.begin(), line.end(), results, pp_block_if)) { // #if
					auto pp_if = createNode(line, _stack.top());
					if (line.find("GL_ES") != std::string::npos)
						pp_if->ppFlag = PPFlag::ppGLES;
					else
						pp_if->ppFlag = PPFlag::ppStart;

					_stack.push(pp_if);

					auto& match = results[0];
					parsePPCondSymbols(match.second, line.end());
				}
				else if (std::regex_search(line.begin(), line.end(), results, pp_block_elif)) { // #elif
					_stack.pop();

					auto pp_elif = createNode(line, _stack.top());
					pp_elif->ppFlag = PPFlag::ppElif;

					_stack.push(pp_elif);

					auto& match = results[0];
					parsePPCondSymbols(match.second, line.end());
				}
				else if (std::regex_search(line.begin(), line.end(), pp_block_else)) { // #else
					_stack.pop();

					auto pp_else = createNode(line, _stack.top());
					pp_else->ppFlag = PPFlag::ppElse;

					_stack.push(pp_else);
				}
				else if (std::regex_search(line.begin(), line.end(), pp_block_endif)) { // #endif
					_stack.pop();

					auto pp_endif = createNode(line, _stack.top());
					pp_endif->ppFlag = PPFlag::ppEnd;
				}


				/* TODO - LIST:
				*   - record preprocessor blocks
				*   - fix reserved sample code (regex)
				*   - put all staged uniforms to vs_ub, fs_ub, then insert before first func del
				*/
				else if (std::regex_search(line.begin(), line.end(), func_decl_exp)) { // func decl
					if (_firstFuncNum == -1)
						_firstFuncNum = line_count;
					auto node = createNode(line, _stack.top());
					node->isFuncDecl = true;
					node->isMainDecl = std::regex_search(line.begin(), line.end(), main_decl_exp);
				}
				else {
					createNode(line, _stack.top());
				}
			}
			else { // put empty line directly
				createNode(line, _stack.top());
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

	struct ASTVisitContext {
		ASTNode* uniformBlock = nullptr;
		std::map<std::string_view, ASTNode*> uinformParents;
		int firstFuncDeclIdx = -1; // must valid
		int mainDeclIdx = -1; // must valid
		bool removingPPGLES = false;
	};

	// funcDecl not in AST root was ingored
	void modifyAST() {
		// fix preprocess check syntax
		ASTVisitContext context;

		modifyAST_r(_AST, nullptr, nullptr, context);

		int diff = 0;
		if (context.uniformBlock) {
			assert(context.firstFuncDeclIdx != -1);
			createNode("};\n\n", context.uniformBlock);
			_AST->children.insert(_AST->children.begin() + context.firstFuncDeclIdx, context.uniformBlock);
			++diff;

			for (auto& item : context.uinformParents) {
				auto ppNode = item.second;
				auto ppp = ppNode->parent;
				assert(ppp);
				if (!ppNode->name.empty()) {
					auto ppEndNode = createNode(ppNode->ppend, nullptr);
					auto it = std::find(ppp->children.begin(), ppp->children.end(), ppNode);
					assert(it != ppp->children.end());
					ppp->children.insert(it + 1, ppEndNode);
				}
			}
		}

		assert(context.mainDeclIdx != -1);
		if (_is_frag) {
			_AST->children.insert(_AST->children.begin() + context.mainDeclIdx + diff,
				createNode("layout(location = 0) out vec4 FragColor;\n\n"sv, nullptr));
		}
		// insert version decl code to AST root
		if (_is_frag) {
			_AST->name = "#version 310 es\nprecision highp float;\nprecision highp int;\n"sv;
		}
		else {
			_AST->name = "#version 310 es\n"sv;
		}
	}

	void modifyAST_r(ASTNode* my, ASTNode* parentNext, ASTNode* parent, ASTVisitContext& context) {

		if (my->ppFlag == PPFlag::ppGLES) {
			// ignore me and all children
			my->toRemove = true;// 
			context.removingPPGLES = true;
			return;
		}
		else if (my->isNonSamplerUniform)
		{ // 
			// remove from parent, duplicate a parent
			assert(parent); // must have parent


			auto pos = my->name.find("uniform");
			assert(pos != std::string::npos);
			std::string mutableLine{my->name};
			replace_once(mutableLine, "uniform", "   ");
			my->name = cachestr(mutableLine);


			if (!context.uniformBlock) {
				auto ub_start_code = fmt::format("layout(std140, binding = 0) uniform {} {{\n", _is_frag ? "fs_ub" : "vs_ub");
				context.uniformBlock = createNode(cachestr(ub_start_code), nullptr);
			}

			if (parent->name == "*") {
				context.uniformBlock->addChild(my);
			}
			else {
				ASTNode* newParent = nullptr;

				// needs root parent
				auto it = context.uinformParents.find(parent->name);
				if (it == context.uinformParents.end()) {
					ASTNode* newGrand = nullptr;

					auto grand = parent->parent;
					if (grand && grand->parent != nullptr) { // have grand?
						auto it = context.uinformParents.find(grand->name);
						if (it == context.uinformParents.end()) {
							newGrand = createNode(grand->name, context.uniformBlock);
							if (grand->parent) { // must have parent?
								auto grand_it = std::find(grand->parent->children.begin(), grand->parent->children.end(), grand);
								if (++grand_it != grand->parent->children.end())
									newGrand->ppend = (*grand_it)->name;
							}
							context.uinformParents.emplace(grand->name, newGrand);
						}
						else
							newGrand = it->second;
					}
					if (!newGrand)
						newGrand = context.uniformBlock;
					newParent = createNode(parent->name, newGrand);
					context.uinformParents.emplace(parent->name, newParent);
					if (parentNext)
						newParent->ppend = parentNext->name;
				}
				else
					newParent = it->second;
				newParent->addChild(my);
			}

			my->toRemove = true; // remove from old AST

			return;
		}
		else {
			if (context.removingPPGLES) { // #else, expand $else to parent
				if (my->ppFlag == PPFlag::ppElse)
					my->name = ""sv; // simple clear code line
				else if (my->ppFlag == PPFlag::ppEnd) {
					my->name = ""sv;
					context.removingPPGLES = false;
				}
			}
		}

		size_t index = 0;
		for (auto iter = my->children.begin(); iter != my->children.end();) {
			ASTNode* mynext = nullptr;
			if (parent) {
				auto it = std::find(parent->children.begin(), parent->children.end(), my);
				if (it + 1 != parent->children.end())
					mynext = *(it + 1);
			}
			modifyAST_r(*iter, mynext, my, context);
			if ((*iter)->toRemove) {
				iter = my->children.erase(iter);
				continue;
			}
			if ((*iter)->isFuncDecl && context.firstFuncDeclIdx == -1)
				context.firstFuncDeclIdx = index;
			if ((*iter)->isMainDecl)
				context.mainDeclIdx = index;
			++index;
			++iter;
		}
	}

	void dumpAST(std::string& code) {
		dumpAST_r(_AST, code);
	}

	static void dumpAST_r(ASTNode* p, std::string& str) {
		str += p->name;

		for (auto c : p->children)
			dumpAST_r(c, str);
	}

	void insertLocation(std::string& mutableLine, int& curLoc) {
		std::string_view linesv(mutableLine);
		std::match_results<std::string_view::const_iterator> results;
		if (std::regex_search(linesv.begin(), linesv.end(), results, var_name_exp))
		{
			auto& match = results[0];
			auto first = std::addressof(*match.first);
			auto count = static_cast<size_t>(std::distance(match.first, match.second));
			std::string_view varName {first, count};
			auto lastoff = varName.find_last_not_of(' ', varName.length() - 1);
			assert(lastoff != std::string_view::npos);
			varName.remove_suffix(varName.length() - lastoff);

			auto key = hashstr32(varName);
			auto it = _locationMap.find(key);
			auto loc = it == _locationMap.end() ? curLoc++ : it->second;
			mutableLine.insert(0, fmt::format("layout(location = {}) ", loc));
			_locationMap.emplace(key, loc);
		}
	}

	void parsePPCondSymbols(std::string_view::iterator first, std::string_view::iterator last) {
		// modify shader manually is easier
	}

	template<typename _MatchResult>
	std::string getMatchedMutableLine(const _MatchResult& rets, std::string_view line, size_t& off) {
		auto& match = rets[0];
		auto first = std::addressof(*match.first);
		auto count = static_cast<size_t>(std::distance(match.first, match.second));
		// std::string mutableLine { lineStart, count}; // Note: unmatched chars will be removed
		off = first - line.data();
		return std::string{line};
	}

	std::string_view cachestr(std::string_view str) {
		if (str.empty()) return ""sv;
		auto k = hashstr64(str);
		auto it = _strCache.find(k);
		if (it != _strCache.end())
			return std::string_view{it->second.data(), it->second.size() - 1};
		else {
			auto& strm = _strCache.emplace(k, yasio::sbyte_buffer{str.begin(), str.end(), std::true_type{}}).first->second;
			strm.push_back('\0');
			return std::string_view{strm.data(), strm.size() - 1};
		}
	}

	// xxh32
	static int hashstr32(std::string_view str)
	{
		return !str.empty() ? XXH32(str.data(), str.length(), 0) : 0;
	}

	static int64_t hashstr64(std::string_view str)
	{
		return !str.empty() ? XXH64(str.data(), str.length(), 0) : 0;
	}

	static void replace_precision_qualifiers(std::string& mutableLine) {
		replace_once(mutableLine, "lowp", "");
		replace_once(mutableLine, "highp", "");
		replace_once(mutableLine, "mediump", "");
	}

	static int replace(std::string& string,
		const std::string_view& replaced_key,
		const std::string_view& replacing_key)
	{
		if (replaced_key == replacing_key)
			return 0;
		int count = 0;
		std::string::size_type pos = 0;
		const size_t predicate = !replaced_key.empty() ? 0 : 1;
		while ((pos = string.find(replaced_key, pos)) != std::wstring::npos)
		{
			(void)string.replace(pos, replaced_key.length(), replacing_key);
			pos += (replacing_key.length() + predicate);
			++count;
		}
		return count;
	}

	static bool replace_once(std::string& string,
		const std::string_view& replaced_key,
		const std::string_view& replacing_key)
	{
		std::string::size_type pos = 0;
		if ((pos = string.find(replaced_key, pos)) != std::string::npos)
		{
			(void)string.replace(pos, replaced_key.length(), replacing_key);
			return true;
		}
		return false;
	}

};

extern void save_file(std::string_view path, const std::vector<std::string_view>& chunks);
int migrate_shader_source_one_ast(std::string& shader_source, const std::string& outpath) {
#if 0
	if (outpath.find("label_outline.frag") == std::string::npos)
		return 0;
#endif
	GlslParseContext context;

	// parseAST
	context.parseAST(shader_source, outpath);


	// modify ast
	context.modifyAST();

	// dump ast
	std::string code;
	context.dumpAST(code);

	save_file(outpath, std::vector<std::string_view>{code});

	return 1;
}
