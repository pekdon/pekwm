//
// pekwm_cfg.cc for pekwm
// Copyright (C) 2021 Claes Nästén <pekdon@gmail.com>
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#include "Compat.hh"
#include "CfgParser.hh"
#include "Util.hh"

#include <map>
#include <string>

extern "C" {
#include <getopt.h>
}


static void usage(const char* name, int ret)
{
    std::cout << "usage: " << name << " [-j]" << std::endl;
    std::cout << "  -j --json file    dump file as JSON" << std::endl;
    exit(ret);
}

static void
jsonDumpSection(CfgParser::Entry *entry)
{
    // map keeping track of seen section names to ensure unique names
    // in the output.
    std::map<std::string, int> sections;

    auto it = entry->begin();
    for (; it != entry->end(); ++it) {
        if (it != entry->begin()) {
            std::cout << ",";
        }

        if ((*it)->getSection()) {
            auto name = (*it)->getName();
            if (! (*it)->getValue().empty()) {
                name += "-" + (*it)->getValue();
            }

            std::map<std::string, int>::iterator s_it = sections.find(name);
            if (s_it == sections.end()) {
                sections[name] = 0;
            } else {
                name += "-" + std::to_string(s_it->second);
                s_it->second = s_it->second + 1;
            }

            std::cout << "\"" << name << "\": {" << std::endl;
            jsonDumpSection((*it)->getSection());
            std::cout << "}" << std::endl;
        } else {
            std::cout << "\"" << (*it)->getName() << "\""
                      << ": \"" << (*it)->getValue() << "\"" << std::endl;
        }
    }
}

static void
jsonDump(const std::string& path,
         const std::map<std::string, std::string> &cfg_env)
{
    CfgParser cfg;
    for (auto it : cfg_env) {
        cfg.setVar(it.first, it.second);
    }
    cfg.parse(path);

    std::cout << "{";
    jsonDumpSection(cfg.getEntryRoot());
    std::cout << "}" << std::endl;
}

int main(int argc, char* argv[])
{
    std::string cfg_path;
    std::map<std::string, std::string> cfg_env;

    static struct option opts[] = {
        {"json", required_argument, NULL, 'd'},
        {"env", required_argument, NULL, 'e'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0}
    };

    int ch;
    while ((ch = getopt_long(argc, argv, "e:j:h", opts, NULL)) != -1) {
        switch (ch) {
        case 'e': {
            std::vector<std::string> vals;
            if (Util::splitString<char>(optarg, vals, "=", 2) == 2) {
                cfg_env["$" + vals[0]] = vals[1];
            } else {
                usage(argv[0], 1);
            }
        }
        case 'j':
            cfg_path = optarg;
            break;
        case 'h':
            usage(argv[0], 0);
            break;
        default:
            usage(argv[0], 1);
            break;
        }
    }

    if (! cfg_path.empty()) {
        jsonDump(cfg_path, cfg_env);
    }

    return 0;
}
