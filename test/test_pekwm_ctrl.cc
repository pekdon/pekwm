#include "test.hh"

#define UNITTEST
#include "pekwm_ctrl.cc"

#include <utility>

class TestPekwmCtrl : public TestSuite {
public:
    TestPekwmCtrl()
        : TestSuite("pekwm_ctrl")
    {
        register_test("sendCmd", TestPekwmCtrl::testSendCmd);
    }

    static void testSendCmd(void) {
        std::vector<std::pair<std::string, int>> bufs;
        auto send_message =
            [&] (Window, AtomName, int,
                 const void *data, size_t size) {
                char *buf = new char[size];
                memcpy(buf, data, size);
                int op = buf[size - 1];
                buf[size - 1] = 0;
                bufs.push_back(std::pair<std::string, int>(buf, op));
                delete [] buf;
                return true;
            };

        // single message
        bufs.clear();
        sendCommand("1 message",
                    None, send_message);
        ASSERT_EQUAL("send 1", 1, bufs.size());
        ASSERT_EQUAL("send 1 buf", "1 message", bufs[0].first);
        ASSERT_EQUAL("send 1 op", 0, bufs[0].second);

        // two messages
        bufs.clear();
        sendCommand("2 messages 012345678",
                    None, send_message);
        ASSERT_EQUAL("send 2", 2, bufs.size());
        ASSERT_EQUAL("send 2 buf 1", "2 messages 01234567", bufs[0].first);
        ASSERT_EQUAL("send 2 op 1", 1, bufs[0].second);
        ASSERT_EQUAL("send 2 buf 2", "8", bufs[1].first);
        ASSERT_EQUAL("send 2 op 2", 3, bufs[1].second);

        // two messages
        bufs.clear();
        sendCommand("3 messages with extra padding 0123456789",
                    None, send_message);
        ASSERT_EQUAL("send 3", 3, bufs.size());
        ASSERT_EQUAL("send 3 buf 1", "3 messages with ext", bufs[0].first);
        ASSERT_EQUAL("send 3 op 1", 1, bufs[0].second);
        ASSERT_EQUAL("send 3 buf 2", "ra padding 01234567", bufs[1].first);
        ASSERT_EQUAL("send 3 op 2", 2, bufs[1].second);
        ASSERT_EQUAL("send 3 buf 3", "89", bufs[2].first);
        ASSERT_EQUAL("send 3 op 3", 3, bufs[2].second);
    }
};

int
main(int argc, char *argv[])
{
    TestPekwmCtrl testPekwmCtrl;
    TestSuite::main(argc, argv);
}
