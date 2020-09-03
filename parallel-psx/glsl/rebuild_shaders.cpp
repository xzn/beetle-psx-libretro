#include "rebuild_shaders.h"

#include <concepts>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <map>
#include <optional>
#include <unordered_set>
#include <unordered_map>
#include <regex>
#include <deque>

#include "SPIRV/GlslangToSpv.h"
#include "StandAlone/ResourceLimits.h"
#include "glslang/Public/ShaderLang.h"

using namespace std;
using namespace filesystem;
using namespace chrono;
using namespace glslang;
using namespace spv;

// From boost
template <class T>
inline void hash_combine(T &s, T v)
{
	s ^= v + 0x9e3779b9 + (s << 6) + (s >> 2);
}

using Macros = map<DefineName, optional<Val>>;
template <>
struct std::hash<Macros>
{
	size_t operator()(const Macros &m) const
	{
		size_t ret = 0;
		for (auto &p : m)
		{
			hash_combine(ret, hash<Macros::key_type>()(p.first));
			hash_combine(ret, hash<Macros::mapped_type>()(p.second));
		}
		return ret;
	}
};

using FileContent = string;
unordered_map<FileName, FileContent> contents_of_files;
using DefinesPresent = unordered_map<DefineName, bool>;
unordered_map<FileName, DefinesPresent> macros_present_in_files;
using MacrosDone = unordered_map<Macros, bool>;
unordered_map<FileName, MacrosDone> spirv_done_files;
unordered_map<FileName, vector<FileName>> included_files;
unordered_map<FileName, unordered_set<FileName>> included_files_deep;
unordered_map<FileName, file_time_type> files_write_times;

FileContent read_file_to_string(string_view file)
{
	ifstream f(string(file), ios::ate | ios::binary);
	if (!f)
		return {};
	auto size = f.tellg();
	string ret(size, 0);
	f.seekg(0);
	f.read(ret.data(), size);
	size = f.gcount();
	ret.resize(size);
	return ret;
}

bool &get_spirv_done_for_file(FileName file, Macros ms)
{
    return spirv_done_files[file][ms];
}

const FileContent &get_content_of_file(FileName file)
{
    auto it = contents_of_files.find(file);
    if (it == contents_of_files.end())
    {
        auto content = read_file_to_string(file);
        if (!content.size())
        {
            cerr << "Unable to read file " << file << endl;
            exit(1);
        }
        return contents_of_files[file] = move(content);
    }
    return it->second;
}

const vector<FileName> &get_included_files(FileName file)
{
    auto it = included_files.find(file);
    if (it == included_files.end())
    {
        vector<FileName> ret;
        auto &content = get_content_of_file(file);
        FileContent::const_iterator fi = content.cbegin();
        regex include_regex(R"regex(#include\s+"(.+)")regex");
        smatch res;
        while (regex_search(fi, content.cend(), res, include_regex))
        {
            ret.push_back({&*res[1].first, (FileName::size_type)res[1].length()});
            fi = res.suffix().first;
        }
        return included_files[file] = move(ret);
    }
    return it->second;
}

const unordered_set<FileName> &get_included_files_deep(FileName file)
{
    auto it = included_files_deep.find(file);
    if (it == included_files_deep.end())
    {
        unordered_set<FileName> ret{{file}};
        auto &fs = get_included_files(file);
        for (auto &f : fs)
        {
            auto &gs = get_included_files_deep(f);
            ret.insert(gs.begin(), gs.end());
        }
        return included_files_deep[file] = move(ret);
    }
    return it->second;
}

file_time_type get_file_write_time(FileName f)
{
    auto it = files_write_times.find(f);
    if (it == files_write_times.end())
    {
        error_code ec;
        auto t = last_write_time(f, ec);
        if (ec)
        {
            cerr << "Error reading last write time for file " << f << endl;
            exit(1);
        }
        return files_write_times[f] = t;
    }
    return it->second;
}

file_time_type get_latest_file_change_time(FileName f)
{
    file_time_type t = file_time_type::min();
    auto &fs = get_included_files_deep(f);
    for (auto &f : fs)
    {
        auto t2 = get_file_write_time(f);
        if (t < t2)
            t = t2;
    }
    return t;
}

bool is_macro_present_in_file(FileName file, DefineName define)
{
    auto &content = get_content_of_file(file);
    regex ident_regex("[^a-zA-Z_]" + string(define) + "[^a-zA-Z0-9_]");
    return regex_search(content, ident_regex);
}

bool get_macro_present_in_file(FileName file, DefineName define)
{
    auto &defines_present = macros_present_in_files[file];
    auto it = defines_present.find(define);
    if (it == defines_present.end())
        return defines_present[define] = is_macro_present_in_file(file, define);
    return it->second;
}

Macros get_macros_present_in_file(FileName file, Macros macros)
{
    Macros ret;
    for (auto &m : macros)
        if (get_macro_present_in_file(file, m.first))
            ret.insert(m);
    return ret;
}

Macros get_macros_present_in_included_files(FileName file, Macros macros)
{
    Macros ret;
    auto &fs = get_included_files_deep(file);
    for (auto &f : fs)
    {
        auto m = get_macros_present_in_file(f, macros);
        ret.insert(m.begin(), m.end());
    }
    return ret;
}

// Taken from cppreference.com
template <class... Ts>
struct overload : Ts...
{
	using Ts::operator()...;
};
template <class... Ts>
overload(Ts...) -> overload<Ts...>;

vector<Macros> combine_macros_sets(vector<Macros> a, vector<Macros> b)
{
    vector<Macros> ret;
    for (auto &m : a)
        for (auto &n : b)
        {
            auto o = m;
            o.insert(n.begin(), n.end());
            ret.push_back(move(o));
        }
    return ret;
}

vector<Macros> get_macros_sets_combinations(vector<vector<Macros>> c)
{
    if (c.size() == 1)
        return c.front();
    if (c.size() == 0)
        return {};

    auto a = move(c.back());
    c.pop_back();
    auto b = move(c.back());
    c.pop_back();
    c.push_back(combine_macros_sets(move(a), move(b)));
    return get_macros_sets_combinations(move(c));
}

vector<Macros> get_macros_sets_from_defines(Define defines)
{
    return visit(overload{
        [](DefineName v)
        {
            return vector<Macros>{{
                {{
                    v, {}
                }}
            }};
        },
        [](OneOfPtr v)
        {
            vector<Macros> ret;
            for (auto &d: v->defines)
            {
                auto ms = get_macros_sets_from_defines(d);
                ret.insert(ret.end(), ms.begin(), ms.end());
            }
            return ret;
        },
        [](AllOfPtr v)
        {
            vector<vector<Macros>> t;
            for (auto &d: v->defines)
            {
                auto ms = get_macros_sets_from_defines(d);
                t.push_back(move(ms));
            }
            return get_macros_sets_combinations(move(t));
        },
        [](OneOfValPtr v)
        {
            vector<Macros> ret;
            for (auto &d: v->vals)
                ret.push_back({{v->name, d}});
            return ret;
        },
    }, defines);
}

void print_file_and_macros_info(FileName file, const Macros &ms)
{
    cerr << file << ": ";
    bool next = false;
    for (auto &m : ms)
    {
        if (next)
            cerr << ' ';
        cerr << m.first;
        if (m.second)
            cerr << '=' << *m.second;
        next = true;
    }
    cerr << endl;
}

bool iequals(string_view a, string_view b)
{
    return std::equal(a.begin(), a.end(), b.begin(), b.end(),
        [](char a, char b) {
            return tolower(a) == tolower(b);
        }
    );
}

unordered_map<Macros, string> defines_string_for_macro_set;

string gen_defines_string_from_macro_set(const Macros &ms)
{
    ostringstream oss;
    oss << "#extension GL_GOOGLE_include_directive : enable" << endl;
    for (auto &m : ms)
    {
        oss << "#define " << m.first;
        if (m.second)
            oss << ' ' << *m.second;
        oss << '\n';
    }
    return oss.str();
}

const string &get_defines_string_for_macro_set(const Macros &ms)
{
    auto it = defines_string_for_macro_set.find(ms);
    if (it == defines_string_for_macro_set.end())
        return defines_string_for_macro_set[ms] =
            gen_defines_string_from_macro_set(ms);
    return it->second;
}

unordered_map<FileName, TShader::Includer::IncludeResult> include_results_of_files;

struct ShaderIncluder : TShader::Includer
{
    virtual IncludeResult *includeLocal(const char *header, const char *include, size_t depth) override
    {
        FileName file(header);
        auto it = include_results_of_files.find(file);
        if (it == include_results_of_files.end())
        {
            auto &c = get_content_of_file(file);
            auto i = include_results_of_files.emplace(file, IncludeResult{header, c.data(), c.length(), nullptr});
            return &i.first->second;
        }
        return &it->second;
    }

    virtual void releaseInclude(IncludeResult *) override {}
};

using SpvString = std::vector<unsigned int>;
SpvString compile_with_defines(FileName file, const Macros &m, bool print_info)
{
    auto e = file.find_last_of('.');
    if (e == file.npos)
    {
        cerr << "No file extension for " << file << endl;
        exit(1);
    }
    EShLanguage stage;
    auto ext = file.substr(e + 1);
    if (iequals(ext, "vert"))
        stage = EShLangVertex;
    else if (iequals(ext, "frag"))
        stage = EShLangFragment;
    else if (iequals(ext, "comp"))
        stage = EShLangCompute;
    else
    {
        cerr << "Unknown file extension " << ext << " for file " << file << endl;
        exit(1);
    }
    TShader shader(stage);
    TProgram program;
    ShaderIncluder includer;
    EProfile profile = ECoreProfile;
    auto messages = EShMessages(EShMsgDefault | EShMsgSpvRules | EShMsgVulkanRules);
    int default_version = 450;
    auto &preamble = get_defines_string_for_macro_set(m);
    if (print_info)
        cerr << preamble << endl;
    shader.setPreamble(preamble.c_str());
    auto &c = get_content_of_file(file);
    const char* src = c.data();
    int src_len = c.length();
    shader.setStringsWithLengths(&src, &src_len, 1);

    auto on_error = [&](const char* msg) {
        cerr << file << ": " << msg << endl;
        cerr << "Shader info log:" << endl;
        cerr << shader.getInfoLog() << endl;
        cerr << shader.getInfoDebugLog() << endl;
        cerr << "Program info log:" << endl;
        cerr << program.getInfoLog() << endl;
        cerr << program.getInfoDebugLog() << endl;
        exit(1);
    };
    if (!shader.parse(&DefaultTBuiltInResource, default_version, profile, false, true, messages, includer))
        on_error("Failed to parse shader");

    program.addShader(&shader);
    if (!program.link(messages))
        on_error("Failed to link program");

    auto *intermediate = program.getIntermediate(stage);
    if (!intermediate)
        on_error("Failed to generate SPIR-V");

    SpvString out;
    SpvBuildLogger logger;
    GlslangToSpv(*intermediate, out, &logger);
    auto logger_messages = logger.getAllMessages();
    if (logger_messages.size())
        cerr << logger_messages << endl;
    return out;
}

// From cppreference
string tolower(string s) {
    std::transform(s.begin(), s.end(), s.begin(), 
        [](unsigned char c) { return tolower(c); }
    );
    return s;
}

string get_file_with_defines_name(FileName file, Macros ms)
{
    ostringstream oss;
    auto d = file.find_last_of('.');
    oss << file.substr(0, d);
    for (auto &m : ms)
    {
        oss << '.';
        oss << tolower(string(m.first));
        if (m.second)
            oss << '_' << *m.second;
    }
    oss << file.substr(d) << ".inc";
    return oss.str();
}

// From Stack Overflow
template <typename T>
time_t to_time_t(T t)
{
    auto s = time_point_cast<system_clock::duration>(t - T::clock::now() + system_clock::now());
    return system_clock::to_time_t(s);
}

void compile_with_defines(FileName file, const vector<Macros> &m, bool print_info)
{
    auto t = get_latest_file_change_time(file);
    if (print_info)
    {
        auto t2 = to_time_t(t);
        cerr << file << " last changed " << put_time(localtime(&t2), "%Y-%m-%d %H:%M:%S") << endl;
    }
    for (auto &ms : m)
    {
        auto ns = get_macros_present_in_included_files(file, ms);
        auto &done = get_spirv_done_for_file(file, ns);
        if (done)
            continue;
        if (print_info)
            print_file_and_macros_info(file, ns);
        auto spv = compile_with_defines(file, ns, print_info);
        done = true;
    }
}

vector<Macros> filter_zeros_defines(vector<Macros> m)
{
    unordered_set<Macros> seen;
    for (auto &ms : m)
    {
        vector<Macros::value_type> n;
        copy_if(ms.begin(), ms.end(), back_inserter(n), [](Macros::value_type v)
        {
            return v.first.size();
        });
        Macros o;
        o.insert(n.begin(), n.end());
        seen.insert(o);
    }
    vector<Macros> ret;
    ret.insert(ret.end(), seen.begin(), seen.end());

    sort(ret.begin(), ret.end(), [](Macros m, Macros n)
    {
        auto i = m.begin();
        auto j = n.begin();
        while (1)
        {
            if (i == m.end())
                return true;
            if (j == n.end())
                return false;
            if (i->first != j->first)
                return i->first < j->first;
            if (i->second != j->second)
                return i->second < j->second;
            ++i;
            ++j;
        }
    });
    return ret;
}

void generate_program(const Program &p)
{
    auto ms = get_macros_sets_from_defines(p.defines);
    ms = filter_zeros_defines(move(ms));
    for (auto &f : p.files)
        compile_with_defines(f, ms, false);
}

int main()
{
    if (!InitializeProcess())
    {
        cerr << "Failed to initialize glslang." << endl;
        exit(1);
    }

    for (auto &p : shader_list.programs)
        generate_program(p);

    FinalizeProcess();
    return 0;
}
