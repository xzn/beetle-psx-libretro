#include "rebuild_shaders.h"

#include <concepts>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <unordered_set>
#include <unordered_map>
#include <regex>

using namespace std;
namespace fs = filesystem;

using FileName = string_view;
using DefineName = string_view;

// From boost
template <class T>
inline void hash_combine(T &s, T v)
{
	s ^= v + 0x9e3779b9 + (s << 6) + (s >> 2);
}

using Macros = map<DefineName, Val>;
template <>
struct std::hash<Macros>
{
	size_t operator()(const Macros &m) const
	{
		size_t ret = 0;
		for (auto &p : m)
		{
			hash_combine(ret, hash<DefineName>()(p.first));
			hash_combine(ret, hash<Val>()(p.second));
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

FileContent read_file_to_string(string_view file)
{
	ifstream f(file.data(), ios::ate | ios::binary);
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

bool is_macro_present_in_file(FileName file, DefineName define)
{
    auto &content = get_content_of_file(file);
    regex ident_regex(string("[^a-zA-Z_]") + define.data() + "[^a-zA-Z0-9_]");
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
        [](string_view v)
        {
            return vector<Macros>{{
                {{
                    v, ""
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

void print_file_and_macros_info(FileName file, Macros ms)
{
    cerr << file << ": ";
    for (auto &m : ms)
        visit(overload{
            [&](auto a)
            {
                cerr << m.first << " = " << a << ", ";
            },
            [&](string_view a)
            {
                if (a.size())
                    cerr << m.first << " = " << a << ", ";
                else
                    cerr << m.first << ", ";
            },
        }, m.second);
    cerr << endl;
}

void compile_with_defines(FileName file, const vector<Macros> &m)
{
    for (auto &ms : m)
    {
        auto ns = get_macros_present_in_file(file, ms);
        auto &done = get_spirv_done_for_file(file, ns);
        if (done)
            continue;
        print_file_and_macros_info(file, ns);
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

int generate_program(const Program &p)
{
    auto ms = get_macros_sets_from_defines(p.defines);
    ms = filter_zeros_defines(move(ms));
	visit(overload{
        [&](Graphic f)
        {
            compile_with_defines(f.first, ms);
            compile_with_defines(f.second, ms);
        },
        [&](Compute f)
        {
            compile_with_defines(f, ms);
        },
    }, p.files);
	return 0;
}

int main()
{
	for (auto &p : shader_list.programs)
	{
		int ret = generate_program(p);
		if (ret)
			return ret;
	}
	return 0;
}
