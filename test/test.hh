#ifndef _TEST_HH_
#define _TEST_HH_

#include <functional>
#include <iostream>
#include <map>
#include <vector>
#include <string>
#include <sstream>

class AssertFailed {
public:
    AssertFailed(std::string file, int line, std::string msg)
        : _file(file), _line(line), _msg(msg) {
    }

    const std::string& file(void) const { return _file; }
    const int line(void) const { return _line; }
    const std::string& msg(void) const { return _msg;}

private:
    std::string _file;
    int _line;
    std::string _msg;
};

#define ASSERT_EQUAL(msg, expected, actual)                             \
    if ((expected) != (actual)) {                                       \
        std::ostringstream oss;                                         \
        oss << (msg)                                                    \
            << " expected " << (expected)                               \
            << " got " << (actual);                                     \
        throw AssertFailed(__FILE__, __LINE__, oss.str());              \
    }

class TestSuite {
public:
    typedef std::function<void()> test_fn;

    TestSuite(const std::string& name)
        : _name(name)
    {
         _suites.push_back(this);
    }
    virtual ~TestSuite() { }

    static int main(int, char **)
    {
        bool status = true;

        std::vector<TestSuite*>::iterator it(_suites.begin());
        for (; it != _suites.end(); ++it ) {
            status = (*it)->test() && status;
        }

        return status ? 0 : 1;
    }

    const std::string& name() const { return _name; }

    void register_test(const std::string& name, test_fn fn)
    {
        _tests[name] = fn;
    };

    bool test()
    {
        bool status = true;

        std::cout << _name << std::endl;
        std::map<std::string, test_fn>::iterator it(_tests.begin());
        for (; it != _tests.end(); ++it) {
            try {
                std::cout << "  * " << it->first << "...";
                it->second();
                std::cout << " OK" << std::endl;;
            } catch (const AssertFailed& ex) {
                std::cout << " ERROR" << std::endl;;
                std::cout << "      " << ex.file() << ":" << ex.line() << " "
                          << _name << "::" << it->first << " " << ex.msg()
                          << std::endl;
                status = false;
            }
        }
        return status;
    }

private:
    std::string _name;
    std::map<std::string, test_fn> _tests;

    static std::vector<TestSuite*> _suites;
};

std::vector<TestSuite*> TestSuite::_suites;

#endif // _TEST_HH_
