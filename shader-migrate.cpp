// Author: https://github.com/DelinWorks/
#include <iostream> 
#include <fstream>
#include <string>
#include <vector>
#include "fmt/compile.h"
#include <map>
#include "yasio/string_view.hpp"

using namespace std::string_view_literals;

namespace helper {
    inline bool replace(std::string& str, const std::string& from, const std::string& to) {
        size_t start_pos = str.find(from);
        if (start_pos == std::string::npos)
            return false;
        str.replace(start_pos, from.length(), to);
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

    inline void pack_vector_string_compact(std::string& str, std::vector<std::string>& lines) {
        str = "";
        for (auto& _ : lines)
            if (_.size() > 0)
                str += _ + '\n';
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

typedef std::map<int, std::vector<std::string>> UBOIndexMap;

UBOIndexMap generate_any_stage_ubo_indexer(std::string& vertex_shader) {
    std::vector<std::string> lines;

    UBOIndexMap UBOsymbols;
    bool isInUBO = false;
    bool currentBoundUBOIndex = 0;
    int UBOIndex= 0;

    helper::split(vertex_shader, "\n", lines);

#define CHECK_UNSET_UBO_BLOCKS_CONTINUE(I) if (checkUnsetUBO(I)) continue;

    auto checkUnsetUBO = [&](int* i) -> bool
    {
        if (isInUBO) {
            (*i)++;
            isInUBO = false;
            return true;
        }
        return false;
    };

    for (int i = 0; i < lines.size(); i++) {
        auto line = lines[i];
        helper::trim(line);

        if (line.starts_with("uniform")) {
            std::vector<std::string> columns;
            helper::split(line, " ", columns);

            if (columns.size() < 2)
                PARSE_ERROR_CONTINUE("Uniform Attribute", i);

            std::string datatype = columns[1];
            std::string varname = columns[2];

            if (datatype == "sampler2D" || datatype == "samplerCube") {
                CHECK_UNSET_UBO_BLOCKS_CONTINUE(&i); i++;
                continue;
            }

            line = "";

            auto checkUniformInternalPrecalculation = [](std::string& uniform) {
                if (uniform.find("[") != std::string::npos && !uniform.ends_with("];"))
                    uniform += "];";
            };

            if (!isInUBO) {
                UBOsymbols.insert({ UBOIndex, {} });
                isInUBO = true;
                std::string temp = fmt::format("    {} {}", datatype, varname);
                checkUniformInternalPrecalculation(temp);
                UBOsymbols.find(UBOIndex)->second.push_back(temp);
                currentBoundUBOIndex = UBOIndex++;
                continue;
            }

            std::string temp = fmt::format("    {} {}", datatype, varname);
            checkUniformInternalPrecalculation(temp);
            UBOsymbols.find(currentBoundUBOIndex)->second.push_back(temp);
        }
        else CHECK_UNSET_UBO_BLOCKS_CONTINUE(&i);
    }

    return UBOsymbols;
}

UBOIndexMap merge_ubo_map_unique(UBOIndexMap& i1, UBOIndexMap& i2) {
    UBOIndexMap ubo_indexer;

    auto merge = [&](UBOIndexMap& target) {
        for (auto& _ : target) {
            if (ubo_indexer.find(_.first) == ubo_indexer.end())
                ubo_indexer.insert({ _.first, {} });

            for (auto& __ : _.second) {
                auto& uniforms = ubo_indexer.find(_.first)->second;
                auto it = std::find(uniforms.begin(), uniforms.end(), __);
                if (it == uniforms.end())
                    uniforms.push_back(__);
            }
        }
    };

    merge(i1);
    merge(i2);

    return ubo_indexer;
}

void parse_vertex_100_310(std::string& vertex_shader, UBOIndexMap& ubo_indexer) {
    std::vector<std::string> lines;

    int currentUBOIndex = 0;

    int currentIndentLevel = 0;

    int locationIn = 0;
    int locationOut = 0;
    int locationUniform = 0;

    // split will ignore empty lines
    helper::split(vertex_shader, "\n", lines);

    for (int i = 0; i < lines.size(); i++) {
        auto& line = lines[i];
        helper::trim(line);

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

        if (line.starts_with("#ifdef GL_ES")) {

            for (int d = i; d < lines.size(); d++) {
                if (lines[d].starts_with("#else"))
                {
                    lines[d] = "";

                    for (int e = i; e < lines.size(); e++) {
                        if (lines[e].starts_with("#endif")) {
                            lines[e] = "";
                            break;
                        }
                    }
                    break;
                }
                lines[d] = "";
            }
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
        }

        if (line.starts_with("varying")) {
            std::vector<std::string> columns;
            helper::split(line, " ", columns);
            line = "";

            if (columns.size() < 2)
                PARSE_ERROR_CONTINUE("Varying Attribute", i);

            columns[2] = columns[2].substr(0, columns[2].size() - 1);

            std::string datatype = columns[1];
            std::string varname = columns[2];
            std::string location = std::to_string(locationOut++);

            line = fmt::format("layout (location = {}) out {} {};", location, datatype, varname);
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
                line = fmt::format("layout (location = {}, binding = 0) uniform {} {};", index, datatype, varname);
                continue;
            }

            line = "";

            if (currentUBOIndex < ubo_indexer.size()) {
                auto& uniforms = ubo_indexer.find(currentUBOIndex)->second;
                int lineOffsetIndex = 0;
                lines.insert(lines.begin() + i + lineOffsetIndex++, "\nlayout(std140, binding = 0) uniform UBO_" +
                    std::to_string(currentUBOIndex) + " {");
                for (auto& _ : uniforms)
                    lines.insert(lines.begin() + i + lineOffsetIndex++, _);
                lines.insert(lines.begin() + i + lineOffsetIndex, "};\n");
                currentUBOIndex++;
                i += lineOffsetIndex - 1;
            }

            /*if (!isInUBO) {
                std::string index = std::to_string(UBOIndex);
                UBOsymbols.insert({ UBOIndex, {} });
                std::string head = "\nlayout(std140, binding = 0) uniform UBO_" + index + " {";
                lines.insert(lines.begin() + i, head);
                isInUBO = true;
                std::string temp;
                lines[1 + i++] = temp = fmt::format("    {} {};", datatype, varname);
                UBOsymbols.find(UBOIndex)->second.push_back(temp);
                currentBoundUBOIndex = UBOIndex++;
                continue;
            }*/

            //line = fmt::format("    {} {};", datatype, varname);
        }

        if (line.starts_with("void main") && currentUBOIndex < ubo_indexer.size()) {
            while (currentUBOIndex < ubo_indexer.size()) {
                auto& uniforms = ubo_indexer.find(currentUBOIndex)->second;
                int lineOffsetIndex = 0;
                lines.insert(lines.begin() + i + lineOffsetIndex++, "\nlayout(std140, binding = 0) uniform UBO_" +
                    std::to_string(currentUBOIndex) + " {");
                for (auto& _ : uniforms)
                    lines.insert(lines.begin() + i + lineOffsetIndex++, _);
                lines.insert(lines.begin() + i + lineOffsetIndex, "};");
                currentUBOIndex++;
                i += lineOffsetIndex + 1;
            }
        }
    }

    helper::pack_vector_string_compact(vertex_shader, lines);
}

void parse_fragment_100_310(std::string& fragment_shader, UBOIndexMap& ubo_indexer) {
    std::vector<std::string> lines;

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

        while (helper::replace(line, "gl_FragColor", "FragColor")) {};
        while (helper::replace(line, "texture2D(", "texture(")) {};
        while (helper::replace(line, "texture2D (", "texture(")) {};
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

        if (line.starts_with("#ifdef GL_ES")) {

            for (int d = i; d < lines.size(); d++) {
                if (lines[d].starts_with("#else"))
                {
                    lines[d] = "";

                    for (int e = i; e < lines.size(); e++) {
                        if (lines[e].starts_with("#endif")) {
                            lines[e] = "";
                            break;
                        }
                    }
                    break;
                }
                lines[d] = "";
            }
        }

        if (line.starts_with("varying")) {
            std::vector<std::string> columns;
            helper::split(line, " ", columns);
            line = "";

            if (columns.size() < 2)
                PARSE_ERROR_CONTINUE("Varying Attribute", i);

            columns[2] = columns[2].substr(0, columns[2].size() - 1);

            std::string datatype = columns[1];
            std::string varname = columns[2];
            std::string location = std::to_string(locationIn++);

            line = fmt::format("layout (location = {}) in {} {};", location, datatype, varname);
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
                line = fmt::format("layout (location = {}, binding = 0) uniform {} {}", index, datatype, varname);
                continue;
            }

            line = "";

            if (currentUBOIndex < ubo_indexer.size()) {
                auto& uniforms = ubo_indexer.find(currentUBOIndex)->second;
                int lineOffsetIndex = 0;
                lines.insert(lines.begin() + i + lineOffsetIndex++, "\nlayout(std140, binding = 0) uniform UBO_" +
                    std::to_string(currentUBOIndex) + " {");
                for (auto& _ : uniforms)
                    lines.insert(lines.begin() + i + lineOffsetIndex++, _);
                lines.insert(lines.begin() + i + lineOffsetIndex, "};\n");
                currentUBOIndex++;
                i += lineOffsetIndex - 1;
            }

            continue;
        }

        if (line.starts_with("void main")) {
            line = fmt::format("layout (location = {}) out {} {};", locationOut++, "vec4", "FragColor") + "\n" + line;

            if (currentUBOIndex < ubo_indexer.size()) {
                while (currentUBOIndex < ubo_indexer.size()) {
                    auto& uniforms = ubo_indexer.find(currentUBOIndex)->second;
                    int lineOffsetIndex = 0;
                    lines.insert(lines.begin() + i + lineOffsetIndex++, "\nlayout(std140, binding = 0) uniform UBO_" +
                        std::to_string(currentUBOIndex) + " {");
                    for (auto& _ : uniforms)
                        lines.insert(lines.begin() + i + lineOffsetIndex++, _);
                    lines.insert(lines.begin() + i + lineOffsetIndex, "};");
                    currentUBOIndex++;
                    i += lineOffsetIndex + 1;
                }
            }
        }
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
    UBOIndexMap ubo_indexer = generate_any_stage_ubo_indexer(shader_source);

    auto is_frag = shader_source.find("gl_FragColor") != std::string::npos;

    if(cxx20::ic::ends_with(outpath, ".frag"sv) || shader_source.find("gl_FragColor") != std::string::npos) {
        parse_fragment_100_310(shader_source, ubo_indexer);
    }
    else {
        parse_vertex_100_310(shader_source, ubo_indexer);
    }

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
