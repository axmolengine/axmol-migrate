// Author: https://github.com/DelinWorks/
#include <iostream> 
#include <fstream>
#include <string>
#include <vector>
#include "fmt/compile.h"
#include <map>
#include "yasio/string_view.hpp"
#include <regex>
#include <unordered_set>
#include <unordered_map>

using namespace std::string_view_literals;

namespace helper {
    int hash_function(std::string key) {
        int hashCode = 0;
        for (int i = 0; i < key.length(); i++) {
            hashCode += key[i] | i * 1024 >> 2;
        }
        return abs(hashCode);
    }

    inline bool replace(std::string& str, const std::string& from, const std::string& to) {
        size_t start_pos = str.find(from);
        if (start_pos == std::string::npos)
            return false;
        str.replace(start_pos, from.length(), to);
        return true;
    }

    inline bool replace(std::string& str, const char from, const std::string& to) {
        size_t start_pos = str.find(from);
        if (start_pos == std::string::npos)
            return false;
        str.replace(start_pos, 1, to);
        return true;
    }

    inline void ltrim(std::string& s) {
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
            return !std::isspace(ch);
            }));
    }

    inline void rtrim(std::string& s) {
        s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
            return !std::isspace(ch);
            }).base(), s.end());
    }

    inline void trim(std::string& s) {
        rtrim(s);
        ltrim(s);
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

    inline void left_pad(std::string& str, int numSpaces)
    {
        int paddedLength = str.length() + numSpaces;
        std::string paddedStr(paddedLength, ' ');
        paddedStr.replace(numSpaces, str.length(), str);
        str = std::move(paddedStr);
    }

    int char_occurrences(const std::string& str, char ch) {
        int count = 0;
        for (char c : str)
            if (c == ch)
                count++;
        return count;
    }

    inline std::string remove_substring(const std::string& input, const char from, const char until) {
        std::size_t startPos = input.find(from); // Find the position of '['
        if (startPos != std::string::npos) {
            std::size_t endPos = input.find(until, startPos + 1); // Find the position of ']' after '['
            if (endPos != std::string::npos) {
                return input.substr(0, startPos) + input.substr(endPos + 1);
            }
        }
        return input; // Return the original string if '[' or ']' is not found
    }

    inline std::string extract_substring(const std::string& input, const char from, const char until) {
        std::size_t startPos = input.find(from); // Find the position of '['
        if (startPos != std::string::npos) {
            std::size_t endPos = input.find(until, startPos + 1); // Find the position of ']' after '['
            if (endPos != std::string::npos) {
                return input.substr(startPos, endPos);
            }
        }
        return "";
    }

    inline void pack_vector_string_compact(std::string& str, std::vector<std::string>& lines) {
        str = "";
        for (auto& _ : lines)
            if (_.size() > 0)
                str += std::regex_replace(_, std::regex(R"(\\s+)"), " ") + '\n';
    }
}

void load_shader_source(const std::string& path, std::string& out) {
    std::fstream file;
    file.open(path);
    if (file.is_open()) {
        std::string tp;
        while (std::getline(file, tp)) {
            out += tp + "\n";
        }
        file.close();
    }
}

void save_shader_source(const std::string& path, std::string& in) {
    std::ofstream modified;
    modified.open(path, std::ofstream::out | std::ofstream::trunc);
    modified << in;
    modified.close();
}

#define PARSE_ERROR_CONTINUE(T, I) do { std::cout << fmt::format("Warning: {} at line {} couldn't be parsed", T, I) << std::endl; continue; } while (0);

void parse_vertex_100_310(std::string& vertex_shader) {
    std::vector<std::string> lines;
    std::unordered_map<std::string, std::string> used_varyings;

    int currentIndentLevel = 0;

    int locationIn = 0;
    int locationOut = 0;
    int locationUniform = 0;

    // split will ignore empty lines
    helper::split(vertex_shader, "\n", lines);

    for (int i = 0; i < lines.size(); i++) {
        auto& line = lines[i];
        helper::trim(line);

        while (helper::replace(line, "lowp ", ""));
        while (helper::replace(line, "mediump ", ""));
        while (helper::replace(line, "highp ", ""));
        while (helper::replace(line, "precision float;", ""));
        while (helper::replace(line, "texColor.rgb(texColor.a)", "texColor.rgb * texColor.a"));

        line = std::regex_replace(line, std::regex(R"(\\s+)"), " ");

        if (i == 0 && !line.starts_with("#version 310 es")) {
            lines.insert(lines.begin() + 0, "#version 310 es");
            lines.insert(lines.begin() + 1, "precision highp float;");
            lines.insert(lines.begin() + 2, "precision highp int;\n");
            i += 2;
            continue;
        }
        else if (i == 0)
        {
            std::cout << "Vertex shader is already in glsl 310 es format." << std::endl;
        }

        if (line.find('=') != std::string::npos &&
            line.find("!=") == std::string::npos &&
            line.find("+=") == std::string::npos &&
            line.find("-=") == std::string::npos &&
            line.find("*=") == std::string::npos &&
            line.find("/=") == std::string::npos &&
            line.find(">=") == std::string::npos &&
            line.find("<=") == std::string::npos)
        {
            std::vector<std::string> columns;
            helper::split(line, "=", columns);

            if (columns.size() != 2)
                PARSE_ERROR_CONTINUE("Unusual assigment '=' operator, Ignored as this might be a comparison operator", i);

            for (auto& _ : columns)
                helper::trim(_);

            std::vector<std::string> lcolumns;
            helper::split(columns[0], " ", lcolumns);
            std::string datatype = lcolumns[0];

            if (!columns[1].starts_with(datatype) && lcolumns.size() == 2)
            {
                columns[1] = datatype + "(" + columns[1];
                helper::replace(columns[1], ";", ");");
                line = fmt::format("{}= {}", columns[0], columns[1]);
            }

            continue;
        }

        //if (line.starts_with("#ifdef GL_ES")) {

        //    for (int d = i; d < lines.size(); d++) {
        //        if (lines[d].starts_with("#endif")) {
        //            lines[d] = "";
        //            break;
        //        }
        //        if (lines[d].starts_with("#else"))
        //        {
        //            lines[d] = "";

        //            for (int e = i; e < lines.size(); e++) {
        //                if (lines[e].starts_with("#endif")) {
        //                    lines[e] = "";
        //                    break;
        //                }
        //            }
        //            break;
        //        }
        //        lines[d] = "";
        //    }
        //    continue;
        //}

        if (line.starts_with("#if") && !line.starts_with("#ifdef")) {

            auto parseMacro = [&](std::string& m) {
                std::string cline = m.substr(0);

                while (helper::replace(cline, "#ifdef ", ""));
                while (helper::replace(cline, "#ifndef ", ""));
                while (helper::replace(cline, "#if ", ""));
                while (helper::replace(cline, "#ifdef", ""));
                while (helper::replace(cline, "#ifndef", ""));
                while (helper::replace(cline, "#if", ""));

                std::string forbidden_chars = "<>()0123456789";

                for (int i = 0; i < forbidden_chars.length(); i++)
                    while (helper::replace(cline, forbidden_chars[i], ""));

                std::string macro = cline;
                helper::trim(macro);
                std::string newMacro = fmt::format(" defined({}) && {}", macro, macro);

                helper::replace(line, macro, newMacro);
                while (helper::replace(line, "#ifdef", "#if"));
                while (helper::replace(line, "#ifndef", "#if"));
            };

            std::string cline = line.substr(0);
            while (helper::replace(cline, "&&", "$SEARCH_MACRO$"));
            while (helper::replace(cline, "||", "$SEARCH_MACRO$"));

            std::vector<std::string> columns;
            helper::split(cline, "$SEARCH_MACRO$", columns);

            for (auto& _ : columns)
                parseMacro(_);

            continue;
        }

        if (line.starts_with("precision"))
            continue; // Nothing to do here.

        if (line.starts_with("attribute")) {
            std::vector<std::string> columns;
            helper::split(line, " ", columns);
            line = "";

            if (columns.size() < 2)
                PARSE_ERROR_CONTINUE("Vertex Attribute", i);

            columns[2] = columns[2].substr(0, columns[2].size() - 1);

            std::string datatype = columns[1];
            std::string varname = columns[2];
            std::string location = std::to_string(locationIn++);

            line = fmt::format("layout (location = {}) in {} {};", location, datatype, varname);

            continue;
        }

        if (line.starts_with("varying")) {
            std::vector<std::string> columns;
            helper::split(line, " ", columns);
            line = "";

            if (columns.size() < 2)
                PARSE_ERROR_CONTINUE("Varying Attribute", i);

            std::string datatype = columns[1];
            std::string varname = columns[2];
            std::string extra = "";
            for (int i = 3; i < columns.size(); i++) extra += columns[i];

            std::string location = std::to_string(locationOut++);

            if (varname.ends_with(';'))
                varname = varname.substr(0, varname.size() - 1);
            if (extra.ends_with(';'))
                extra = extra.substr(0, extra.size() - 1);

            std::string final = fmt::format("layout (location = {}) out {} {} {};", location, datatype, varname, extra);

            if (used_varyings.find(varname) != used_varyings.end())
            {
                line = used_varyings.find(varname)->second;
                locationOut--;
                continue;
            }
            else
                used_varyings.insert({ varname, final });

            line = final;

            continue;
        }

        if (line.starts_with("uniform")) {
            std::vector<std::string> columns;
            helper::split(line, " ", columns);

            if (columns.size() < 2)
                PARSE_ERROR_CONTINUE("Uniform Attribute", i);

            std::string datatype = columns[1];
            std::string varname = columns[2];

            if (datatype == "sampler2D" || datatype == "samplerCube") {
                std::string index = std::to_string(locationUniform++);
                line = fmt::format("layout (binding = 0) uniform {} {}", datatype, varname);
                continue;
            }

            for (int i = 3; i < columns.size(); i++) {
                if (columns[i].find("//") != std::string::npos)
                    break;
                varname += fmt::format(" {} ", columns[i]);
            }

            helper::trim(varname);

            if (varname.ends_with(';'))
                varname = varname.substr(0, varname.size() - 1);
            std::string brackets = helper::extract_substring(varname, '[', ']');
            varname = helper::remove_substring(varname, '[', ']');

            line = "";

            int lineOffsetIndex = 0;
            std::string uHash = " U_" + std::to_string(helper::hash_function(varname));
            for (auto& _ : lines)
                while (helper::replace(_, varname, uHash));
            lines.insert(lines.begin() + i + lineOffsetIndex++, "\nlayout(std140, binding = 0) uniform " + varname + " {");
            lines.insert(lines.begin() + i + lineOffsetIndex++, "    " + datatype + uHash + brackets + "; ");
            lines.insert(lines.begin() + i + lineOffsetIndex++, "};\n");

            continue;
        }
    }

    helper::pack_vector_string_compact(vertex_shader, lines);
}

void parse_fragment_100_310(std::string& fragment_shader) {
    std::vector<std::string> lines;
    std::unordered_map<std::string, std::string> used_varyings;

    std::unordered_set<std::string> used_uniforms;

    int currentUBOIndex = 0;

    int currentIndentLevel = 0;

    int locationIn = 0;
    int locationOut = 0;
    int locationUniform = 0;

    // split will ignore empty lines
    helper::split(fragment_shader, "\n", lines);

    for (int i = 0; i < lines.size(); i++) {
        auto& line = lines[i];
        helper::trim(line);

        while (helper::replace(line, "lowp ", ""));
        while (helper::replace(line, "mediump ", ""));
        while (helper::replace(line, "highp ", ""));
        while (helper::replace(line, "precision float;", ""));
        while (helper::replace(line, "texColor.rgb(texColor.a)", "texColor.rgb * texColor.a"));

        line = std::regex_replace(line, std::regex(R"(\\s+)"), " ");

        while (helper::replace(line, "gl_FragColor", "FragColor")) {};
        while (helper::replace(line, "texture2D(", "texture(")) {};
        while (helper::replace(line, "texture2D (", "texture(")) {};
        while (helper::replace(line, "textureCube(", "texture(")) {};
        while (helper::replace(line, "textureCube (", "texture(")) {};
        while (helper::replace(line, " sample ", " texColor ")) {};
        while (helper::replace(line, "sample.", "texColor.")) {};

        if (i == 0 && !line.starts_with("#version 310 es")) {
            lines.insert(lines.begin() + 0, "#version 310 es");
            lines.insert(lines.begin() + 1, "precision highp float;");
            lines.insert(lines.begin() + 2, "precision highp int;\n");
            i += 2;
            continue;
        }
        else if (i == 0)
        {
            std::cout << "Vertex shader is already in glsl 310 es format." << std::endl;
        }

        if (line.find('=') != std::string::npos &&
            line.find("!=") == std::string::npos &&
            line.find("+=") == std::string::npos &&
            line.find("-=") == std::string::npos &&
            line.find("*=") == std::string::npos &&
            line.find("/=") == std::string::npos &&
            line.find(">=") == std::string::npos &&
            line.find("<=") == std::string::npos)
        {
            std::vector<std::string> columns;
            helper::split(line, "=", columns);

            if (columns.size() != 2)
                PARSE_ERROR_CONTINUE("Unusual assigment '=' operator", i);

            for (auto& _ : columns)
                helper::trim(_);

            std::vector<std::string> lcolumns;
            helper::split(columns[0], " ", lcolumns);
            std::string datatype = lcolumns[0];

            if (!columns[1].starts_with(datatype) && lcolumns.size() == 2)
            {
                columns[1] = datatype + "(" + columns[1];
                helper::replace(columns[1], ";", ");");
                line = fmt::format("{}= {}", columns[0], columns[1]);
            }

            continue;
        }

        //if (line.starts_with("#ifdef GL_ES")) {

        //    for (int d = i; d < lines.size(); d++) {
        //        if (lines[d].starts_with("#endif")) {
        //            lines[d] = "";
        //            break;
        //        }
        //        if (lines[d].starts_with("#else"))
        //        {
        //            lines[d] = "";

        //            for (int e = i; e < lines.size(); e++) {
        //                if (lines[e].starts_with("#endif")) {
        //                    lines[e] = "";
        //                    break;
        //                }
        //            }
        //            break;
        //        }
        //        lines[d] = "";
        //    }
        //}

        if (line.starts_with("#if") && !line.starts_with("#ifdef")) {

            auto parseMacro = [&](std::string& m) {
                std::string cline = m.substr(0);

                while (helper::replace(cline, "#ifdef ", ""));
                while (helper::replace(cline, "#ifndef ", ""));
                while (helper::replace(cline, "#if ", ""));
                while (helper::replace(cline, "#ifdef", ""));
                while (helper::replace(cline, "#ifndef", ""));
                while (helper::replace(cline, "#if", ""));

                std::string forbidden_chars = "<>()0123456789";

                for (int i = 0; i < forbidden_chars.length(); i++)
                    while (helper::replace(cline, forbidden_chars[i], ""));

                std::string macro = cline;
                helper::trim(macro);
                std::string newMacro = fmt::format(" defined({}) && {}", macro, macro);

                helper::replace(line, macro, newMacro);
                while (helper::replace(line, "#ifdef", "#if"));
                while (helper::replace(line, "#ifndef", "#if"));
            };

            std::string cline = line.substr(0);
            while (helper::replace(cline, "&&", "$SEARCH_MACRO$"));
            while (helper::replace(cline, "||", "$SEARCH_MACRO$"));

            std::vector<std::string> columns;
            helper::split(cline, "$SEARCH_MACRO$", columns);

            for (auto& _ : columns)
                parseMacro(_);

            continue;
        }

        if (line.starts_with("varying")) {
            std::vector<std::string> columns;
            helper::split(line, " ", columns);

            if (columns.size() < 2)
                PARSE_ERROR_CONTINUE("Varying Attribute", i);

            std::string datatype = columns[1];
            std::string varname = columns[2];
            std::string extra = "";
            for (int i = 3; i < columns.size(); i++) extra += columns[i];

            std::string location = std::to_string(locationIn++);

            line = "";

            if (varname.ends_with(';'))
                varname = varname.substr(0, varname.size() - 1);
            if (extra.ends_with(';'))
                extra = extra.substr(0, extra.size() - 1);

            std::string final = fmt::format("layout (location = {}) in {} {} {};", location, datatype, varname, extra);

            if (used_varyings.find(varname) != used_varyings.end())
            {
                line = used_varyings.find(varname)->second;
                locationIn--;
                continue;
            }
            else
                used_varyings.insert({ varname, final });

            line = final;
        }

        if (line.starts_with("uniform")) {
            std::vector<std::string> columns;
            helper::split(line, " ", columns);

            if (columns.size() < 2)
                PARSE_ERROR_CONTINUE("Uniform Attribute", i);

            std::string datatype = columns[1];
            std::string varname = columns[2];

            if (datatype == "sampler2D" || datatype == "samplerCube") {
                std::string index = std::to_string(locationUniform++);
                line = fmt::format("layout (binding = 0) uniform {} {}", datatype, varname);
                continue;
            }

            for (int i = 3; i < columns.size(); i++) {
                if (columns[i].find("//") != std::string::npos)
                    break;
                varname += fmt::format(" {} ", columns[i]);
            }

            helper::trim(varname);

            if (varname.ends_with(';'))
                varname = varname.substr(0, varname.size() - 1);
            std::string brackets = helper::extract_substring(varname, '[', ']');
            varname = helper::remove_substring(varname, '[', ']');

            line = "";

            int lineOffsetIndex = 0;
            std::string uHash = " U_" + std::to_string(helper::hash_function(varname));
            for (auto& _ : lines)
                while (helper::replace(_, varname, uHash));
            lines.insert(lines.begin() + i + lineOffsetIndex++, "\nlayout(std140, binding = 0) uniform " + varname + " {");
            lines.insert(lines.begin() + i + lineOffsetIndex++, "    " + datatype + uHash + brackets + "; ");
            lines.insert(lines.begin() + i + lineOffsetIndex++, "};\n");

            continue;
        }

        if (line.starts_with("void main"))
            line = fmt::format("layout (location = {}) out {} {};", locationOut++, "vec4", "FragColor") + "\n" + line;
    }

    helper::pack_vector_string_compact(fragment_shader, lines);
}

void format_any_stage(std::string& source) {
    std::vector<std::string> lines;

    int currentUBOIndex = 0;

    int currentIndentLevel = 0;

    int locationIn = 0;
    int locationOut = 0;
    int locationUniform = 0;

    // split will ignore empty lines
    helper::split(source, "\n", lines);

    for (int i = 0; i < lines.size(); i++)
    {
        auto& line = lines[i];
        helper::trim(line);
        currentIndentLevel -= helper::char_occurrences(line, '}');
        if (currentIndentLevel > 0)
            helper::left_pad(line, currentIndentLevel * 4);
        currentIndentLevel += helper::char_occurrences(line, '{');
    }

    helper::pack_vector_string_compact(source, lines);
}

void migrate_shader_source_one(std::string& shader_source, const std::string& outpath) {
    auto is_frag = cxx20::ic::ends_with(outpath, ".frag"sv) || shader_source.find("gl_FragColor") != std::string::npos;

    if(is_frag)
        parse_fragment_100_310(shader_source);
    else
        parse_vertex_100_310(shader_source);

    format_any_stage(shader_source);

    save_shader_source(outpath, shader_source);
}

#if 0

void migrate_shader_one(const std::string& inpath, const std::string& outpath) {
    std::string shader_source;
    migrate_shader_source_one(shader_source, outpath);
}

int main(int argc, char* argv) {

    std::string vshader_path = "C:/Users/Turky/Desktop/spirv/shader.vert";
    std::string fshader_path = "C:/Users/Turky/Desktop/spirv/shader.frag";

    // std::string vshader_path2 = "C:/Users/Turky/Desktop/spirv/shader2.vert";
    // std::string fshader_path2 = "C:/Users/Turky/Desktop/spirv/shader2.frag";

    std::string vshader_source;
    std::string fshader_source;

    load_shader_source(vshader_path, vshader_source);
    load_shader_source(fshader_path, fshader_source);

    UBOIndexMap vertex_ubo_indexer = generate_any_stage_ubo_indexer(vshader_source);
    UBOIndexMap fragment_ubo_indexer = generate_any_stage_ubo_indexer(fshader_source);

    // Not needed since we can flatten UBOs.
    // UBOIndexMap ubo_indexer = merge_ubo_map_unique(vertex_ubo_indexer, fragment_ubo_indexer);

    parse_vertex_100_310(vshader_source, vertex_ubo_indexer);
    parse_fragment_100_310(fshader_source, fragment_ubo_indexer);

    format_any_stage(vshader_source);
    format_any_stage(fshader_source);

    // save_shader_source(vshader_path2, vshader_source);
    // save_shader_source(fshader_path2, fshader_source);

    std::cout << "converting shaders to gles 310 done!" << std::endl;
}
#endif
